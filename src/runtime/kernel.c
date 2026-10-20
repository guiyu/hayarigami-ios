#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ucontext.h>
#include <time.h>
#include <sys/mman.h>

typedef struct { uint64_t address, original; unsigned arg; int timer; } Gate;
static Gate gates[1024];
static size_t gate_count;
static uint8_t *address_space;
static size_t heap_size;
static struct { uintptr_t base, source; size_t size; unsigned state; } physical[64];
static size_t physical_count;
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;
static int trace_syscalls;
// Virtual reservation, not resident allocation. Standard 4 GiB Switch application pool.
#define RESERVE_SIZE 0x140000000ull
#define HEAP_OFFSET 0x40000000ull
#define POOL_SIZE 0xcd500000ull
static size_t image_size(void) {
    size_t bytes = 0;
    for (size_t i = 0; i < hy_module_count; i++) bytes += hy_modules[i].size;
    return (bytes + 0x1fffff) & ~(size_t)0x1fffff;
}
static uint64_t system_tick(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 19200000 + (uint64_t)t.tv_nsec * 12 / 625;
}
static int memory_syscall(unsigned number, uint64_t *r) {
    if (!address_space) {
        address_space = mmap(NULL, RESERVE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (address_space == MAP_FAILED) hy_fail("guest address reservation");
    }
    switch (number) {
    case 0x06: {
        struct { uint64_t base, size; uint32_t state, attr, perm, ipc, device, padding; } info = {0};
        uint64_t address = r[2], base = (uintptr_t)address_space;
        uint64_t top = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
        size_t stack_size = pthread_get_stacksize_np(pthread_self());
        if (address >= top - stack_size && address < top) {
            info.base = top - stack_size; info.size = stack_size; info.state = 0xb; info.perm = 3;
        } else if (address >= base && address < base + RESERVE_SIZE) {
            uint64_t heap = base + HEAP_OFFSET;
            if (address >= heap && address < heap + heap_size) {
                info.base = heap; info.size = heap_size; info.state = 5; info.perm = 3;
            } else if (address < heap) { info.base = base; info.size = HEAP_OFFSET; }
            else { info.base = heap + heap_size; info.size = base + RESERVE_SIZE - info.base; }
            for (size_t i = 0; i < physical_count; i++) {
                uint64_t start = physical[i].base, end = start + physical[i].size;
                if (address >= start && address < end) {
                    info.base = start; info.size = physical[i].size; info.state = physical[i].state; info.perm = 3; break;
                }
                if (!info.state && start > address && start < info.base + info.size) info.size = start - info.base;
                if (!info.state && end <= address && end > info.base) { info.size -= end - info.base; info.base = end; }
            }
            if (address >= heap && address < heap + heap_size) {
                for (size_t i = 0; i < physical_count; i++) if (physical[i].source) {
                    uint64_t start = physical[i].source, end = start + physical[i].size;
                    if (address >= start && address < end) {
                        info.base = start; info.size = physical[i].size; info.attr = 1; info.perm = 0; break;
                    }
                    if (start > address && start < info.base + info.size) info.size = start - info.base;
                    if (end <= address && end > info.base) { info.size -= end - info.base; info.base = end; }
                }
            }
        } else {
            uint64_t low = 0, high = UINT64_MAX;
            for (size_t i = 0; i < hy_module_count; i++) {
                HyModule *m = &hy_modules[i];
                uint64_t begin = (uintptr_t)m->base, end = begin + m->size;
                if (address >= begin && address < end) {
                    uint64_t rx_end = begin + ((m->text_end + 0x3fff) & ~(size_t)0x3fff);
                    uint64_t patch = begin + m->patch;
                    info.state = 3;
                    if (address < rx_end) { info.base = begin; info.size = rx_end - begin; info.perm = 5; }
                    else if (address < patch) { info.base = rx_end; info.size = patch - rx_end; info.perm = 3; }
                    else { info.base = patch; info.size = end - patch; info.perm = 5; }
                    break;
                }
                if (end <= address && end > low) low = end;
                if (begin > address && begin < high) high = begin;
            }
            if (!info.size) {
                uint64_t starts[] = {base, top - stack_size};
                uint64_t ends[] = {base + RESERVE_SIZE, top};
                for (int i = 0; i < 2; i++) {
                    if (ends[i] <= address && ends[i] > low) low = ends[i];
                    if (starts[i] > address && starts[i] < high) high = starts[i];
                }
                info.base = low; info.size = high - low;
            }
        }
        memcpy((void *)(uintptr_t)r[0], &info, sizeof info);
        r[0] = 0; r[1] = 0; return 1;
    }
    case 0x04: {
        uintptr_t start = r[0], source = r[1], stack = (uintptr_t)address_space + 0x30000000;
        size_t size = r[2]; uintptr_t heap = (uintptr_t)address_space + HEAP_OFFSET;
        if (!size || ((start | source | size) & 4095) || start < stack || start >= stack + 0x10000000 ||
            size > stack + 0x10000000 - start || source < heap || source >= heap + heap_size || size > heap + heap_size - source)
            hy_fail("invalid stack mapping");
        for (size_t i = 0; i < physical_count; i++) {
            if (physical[i].source && source < physical[i].source + physical[i].size && physical[i].source < source + size) {
                r[0] = 0xd801; return 1;
            }
            if (start < physical[i].base + physical[i].size && physical[i].base < start + size)
                hy_fail("overlapping stack mapping");
        }
        if (physical_count == 64) hy_fail("mapping capacity");
        uintptr_t low = start & ~0x3fffull, high = (start + size + 0x3fff) & ~0x3fffull;
        if (mprotect((void *)low, high - low, PROT_READ | PROT_WRITE)) hy_fail("stack map protection");
        memcpy((void *)start, (void *)source, size);
        // Protect complete source pages. Shared 4 KiB edge pages cannot be
        // isolated with Apple's 16 KiB page size; QueryMemory still marks them borrowed.
        uintptr_t source_low = (source + 0x3fff) & ~0x3fffull, source_high = (source + size) & ~0x3fffull;
        if (source_high > source_low && mprotect((void *)source_low, source_high - source_low, PROT_NONE))
            hy_fail("stack source protection");
        physical[physical_count].base = start; physical[physical_count].source = source;
        physical[physical_count].state = 0xb; physical[physical_count++].size = size;
        r[0] = 0; return 1;
    }
    case 0x05: {
        for (size_t i = 0; i < physical_count; i++) if (physical[i].base == r[0] && physical[i].source == r[1] && physical[i].size == r[2]) {
            uintptr_t source_low = (r[1] + 0x3fff) & ~0x3fffull, source_high = (r[1] + r[2]) & ~0x3fffull;
            if (source_high > source_low && mprotect((void *)source_low, source_high - source_low, PROT_READ | PROT_WRITE))
                hy_fail("stack source restore");
            memcpy((void *)(uintptr_t)r[1], (void *)(uintptr_t)r[0], r[2]);
            uintptr_t low = physical[i].base & ~0x3fffull;
            uintptr_t high = (physical[i].base + physical[i].size + 0x3fff) & ~0x3fffull;
            physical[i] = physical[--physical_count];
            for (uintptr_t page = low; page < high; page += 0x4000) {
                int shared = 0;
                for (size_t j = 0; j < physical_count; j++)
                    if (page < physical[j].base + physical[j].size && physical[j].base < page + 0x4000) shared = 1;
                if (!shared && mprotect((void *)page, 0x4000, PROT_NONE)) hy_fail("stack unmap protection");
            }
            r[0] = 0; return 1;
        }
        hy_fail("unknown stack unmap");
    }
    case 0x2c: {
        uintptr_t start = r[0], alias = (uintptr_t)address_space + 0x10000000;
        size_t size = r[1];
        if (!size || (start & 4095) || (size & 4095) || start < alias ||
            start >= alias + 0x20000000 || size > alias + 0x20000000 - start)
            hy_fail("invalid physical mapping");
        for (size_t i = 0; i < physical_count; i++)
            if (start < physical[i].base + physical[i].size && physical[i].base < start + size)
                hy_fail("overlapping physical mapping");
        if (physical_count == 64) hy_fail("physical mapping capacity");
        uintptr_t low = start & ~0x3fffull, high = (start + size + 0x3fff) & ~0x3fffull;
        if (mprotect((void *)low, high - low, PROT_READ | PROT_WRITE)) hy_fail("physical map protection");
        physical[physical_count].base = start; physical[physical_count].source = 0; physical[physical_count].state = 5;
        physical[physical_count++].size = size;
        r[0] = 0; return 1;
    }
    case 0x29: {
        uint64_t value;
        switch (r[1]) {
        case 0: value = 7; break;
        case 1: value = 0xffffffffffffffffull; break;
        case 2: value = (uintptr_t)address_space + 0x10000000; break;
        case 3: value = 0x20000000; break;
        case 4: value = (uintptr_t)address_space + HEAP_OFFSET; break;
        case 5: value = RESERVE_SIZE - HEAP_OFFSET; break;
        case 6: case 21: value = POOL_SIZE; break;
        case 7: case 22: value = heap_size + image_size(); break;
        case 8: case 20: value = 0; break;
        case 11: arc4random_buf(&value, sizeof value); break;
        case 12: value = (uintptr_t)address_space; break;
        case 13: value = RESERVE_SIZE; break;
        case 14: value = (uintptr_t)address_space + 0x30000000; break;
        case 15: value = 0x10000000; break;
        case 16: value = 0x2000000; break;
        case 17: value = 0x400000; break;
        case 18: value = 0x010095b01af94000ull; break;
        case 23: value = 1; break;
        default: hy_fail("unsupported GetInfo type %llu", r[1]);
        }
        r[0] = 0; r[1] = value; return 1;
    }
    case 0x01: {
        size_t requested = r[1];
        if (requested & 0x1fffff) { r[0] = 0xca01; return 1; }
        if (requested > POOL_SIZE - image_size()) { r[0] = 0x10801; return 1; }
        uint8_t *heap = address_space + HEAP_OFFSET;
        for (size_t i = 0; i < physical_count; i++) if (physical[i].source &&
            physical[i].source + physical[i].size > (uintptr_t)heap + requested) { r[0] = 0xd801; return 1; }
        if (requested > heap_size && mprotect(heap + heap_size, requested - heap_size, PROT_READ | PROT_WRITE))
            hy_fail("heap commit");
        if (requested < heap_size && mprotect(heap + requested, heap_size - requested, PROT_NONE))
            hy_fail("heap shrink");
        heap_size = requested; r[0] = 0; r[1] = (uintptr_t)heap; return 1;
    }
    case 0x1e: r[0] = system_tick(); return 1;
    case 0x27:
        if (r[1] > 65536) hy_fail("debug message too long");
        fprintf(stderr, "[guest] %.*s\n", (int)r[1], (const char *)(uintptr_t)r[0]);
        r[0] = 0; return 1;
    case 0x26:
        if (r[0] & 0x80000000) return 1;
        hy_fail("guest Break reason=%#llx result=%#x", r[0], r[1] && r[2] >= 4 ? *(uint32_t *)(uintptr_t)r[1] : 0);
    default: return 0;
    }
}
static int syscall_impl(unsigned number, uint64_t *r) {
    if (hy_thread_syscall(number, r)) return 1;
    pthread_mutex_lock(&memory_lock);
    int handled = memory_syscall(number, r);
    pthread_mutex_unlock(&memory_lock);
    return handled;
}

static void location(uint64_t address) {
    for (size_t i = 0; i < hy_module_count; i++) {
        HyModule *m = &hy_modules[i];
        if (address >= (uintptr_t)m->base && address - (uintptr_t)m->base < m->size) {
            fprintf(stderr, "%s+0x%llx", m->name, address - (uintptr_t)m->base); return;
        }
    }
    fprintf(stderr, "host/unknown 0x%llx", address);
}
static void callers(const uint64_t *r) {
    uintptr_t frame = r[29];
    uintptr_t top = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
    uintptr_t bottom = top - pthread_get_stacksize_np(pthread_self());
    for (int depth = 0; depth < 20 && frame >= bottom && frame <= top - 16 && !(frame & 15); depth++) {
        uint64_t *fp = (uint64_t *)frame;
        fputs("  caller ", stderr); location(fp[1]); fputc('\n', stderr);
        if (fp[0] <= frame) break;
        frame = fp[0];
    }
}
static void crash(int sig, siginfo_t *info, void *context) {
    ucontext_t *u = context;
    fprintf(stderr, "SIGNAL %d address=%p pc=", sig, info->si_addr);
    location(u->uc_mcontext->__ss.__pc);
    fputs(" lr=", stderr); location(u->uc_mcontext->__ss.__lr); fputc('\n', stderr);
    for (unsigned i=0;i<29;i++) fprintf(stderr," x%u=%#llx%s",i,u->uc_mcontext->__ss.__x[i],i%4==3 ? "\n" : "");
    fputc('\n',stderr);
    _Exit(128 + sig);
}
void hy_install_signals(void) {
    const char *trace=getenv("HY_TRACE_SVC"); trace_syscalls=trace && strcmp(trace,"0");
    struct sigaction a = {0}; a.sa_sigaction = crash; a.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &a, NULL); sigaction(SIGBUS, &a, NULL);
    sigaction(SIGILL, &a, NULL); sigaction(SIGTRAP, &a, NULL);
}
void hy_register_gate(uint64_t address, const char *kind, unsigned arg, uint64_t original) {
    int timer = !strcmp(kind, "cntpct") || !strcmp(kind, "cntvct");
    if (!timer && strcmp(kind, "svc")) hy_fail("unsupported gate %s", kind);
    if (gate_count == sizeof gates / sizeof gates[0] || (timer && arg > 31)) hy_fail("gate capacity/register");
    gates[gate_count++] = (Gate){address, original, arg, timer};
}
void hy_thread_init(void) {
    void *tls = calloc(1, 8192); uint8_t *tlr = calloc(1, 0x600);
    if (!tls || !tlr) hy_fail("thread storage allocation");
    *(void **)(tlr + 0x1f8) = tlr + 0x200;
    pthread_setspecific(hy_keys[0], (void *)0x1234);
    uintptr_t tsd; __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(tsd));
    if (((uintptr_t *)(tsd & ~7ull))[hy_keys[0]] != 0x1234) hy_fail("unsupported host TSD layout");
    pthread_setspecific(hy_keys[0], NULL);
    pthread_setspecific(hy_keys[1], tls);
    pthread_setspecific(hy_keys[2], tlr);
    pthread_setspecific(hy_keys[3], (void *)hy_gate);
}
void hy_gate_dispatch(uint64_t address, uint64_t *r) {
    for (size_t i = 0; i < gate_count; i++) {
        Gate *g = &gates[i]; if (g->address != address) continue;
        if (g->timer) {
            if (g->arg < 31) r[g->arg] = system_tick();
            return;
        }
        if (trace_syscalls) {
            fprintf(stderr, "SVC 0x%x at ", g->arg); location(g->original);
            fprintf(stderr, " x0=%#llx x1=%#llx x2=%#llx x3=%#llx\n", r[0], r[1], r[2], r[3]);
        }
        if (g->arg==0x26 && !(r[0]&0x80000000)) {
            fputs("Guest fatal break at ",stderr); location(g->original); fputc('\n',stderr);
            callers(r);
        }
        if (syscall_impl(g->arg, r)) return;
        if (!trace_syscalls) {
            fprintf(stderr, "SVC 0x%x at ", g->arg); location(g->original);
            fprintf(stderr, " x0=%#llx x1=%#llx x2=%#llx x3=%#llx\n", r[0], r[1], r[2], r[3]);
        }
        if (g->arg == 0x1f) fprintf(stderr, "named port: %.12s\n", (char *)(uintptr_t)r[1]);
        callers(r);
        hy_fail("unimplemented SVC 0x%x", g->arg);
    }
    hy_fail("unregistered gate");
}
