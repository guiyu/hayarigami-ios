#include "runtime.h"
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <stdatomic.h>
#include <errno.h>

typedef struct {
    pthread_t pthread;
    pthread_cond_t changed;
    uintptr_t entry, argument, stack, wait_address, condition_key;
    uint32_t handle, wait_tag, wait_result;
    int priority, core, started, exited, waiting;
    uint64_t core_mask;
} Thread;
static Thread *threads[64];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t current_key;
extern _Noreturn void hy_thread_enter(uintptr_t, uintptr_t, uintptr_t);
static Thread *get(uint32_t handle) {
    if (handle == 0xffff8000) return pthread_getspecific(current_key);
    if (handle < 0x100 || handle >= 0x140) return NULL;
    return threads[handle - 0x100];
}
void hy_thread_main(void) {
    if (pthread_key_create(&current_key, NULL)) hy_fail("thread identity key");
    Thread *t = calloc(1, sizeof *t);
    if (!t) hy_fail("main thread allocation");
    pthread_cond_init(&t->changed, NULL);
    t->pthread = pthread_self(); t->handle = 0x100; t->started = 1; t->priority = 44; t->core_mask = 7;
    threads[0] = t; pthread_setspecific(current_key, t);
}
_Noreturn void hy_thread_returned(void) {
    Thread *t = pthread_getspecific(current_key);
    pthread_mutex_lock(&lock); t->exited = 1; pthread_cond_broadcast(&t->changed); pthread_mutex_unlock(&lock);
    pthread_exit(NULL);
}
static void *run(void *argument) {
    Thread *t = argument;
    pthread_setspecific(current_key, t); hy_thread_init();
    hy_thread_enter(t->entry, t->argument, t->stack);
}
// Called with the host registry lock held. Transfer guest mutex ownership
// before waking its waiter; an unlocked interval would allow a lost handoff.
static void unlock_address(uintptr_t address) {
    Thread *next=NULL; unsigned count=0;
    for (size_t i=0;i<64;i++) if (threads[i] && threads[i]->waiting==1 && threads[i]->wait_address==address) {
        if (!next || threads[i]->priority<next->priority) next=threads[i]; count++;
    }
    atomic_store((_Atomic uint32_t *)address,next ? next->wait_tag | (count>1 ? 0x40000000 : 0) : 0);
    if (next) { next->waiting=0; pthread_cond_signal(&next->changed); }
}
int hy_thread_syscall(unsigned number, uint64_t *r) {
    switch (number) {
    case 0x08: {
        Thread *t = calloc(1, sizeof *t); if (!t) hy_fail("thread allocation");
        pthread_cond_init(&t->changed, NULL);
        t->entry = r[1]; t->argument = r[2]; t->stack = r[3] & ~15ull;
        t->priority = (int)r[4]; t->core = (int)r[5];
        if (t->core >= 3 || t->core < -2) hy_fail("invalid thread core");
        t->core_mask = t->core >= 0 ? 1ull << t->core : 7;
        pthread_mutex_lock(&lock);
        unsigned i; for (i = 1; i < 64 && threads[i]; i++) {}
        if (i == 64) hy_fail("thread handle capacity");
        threads[i] = t; t->handle = 0x100 + i;
        pthread_mutex_unlock(&lock);
        r[0] = 0; r[1] = t->handle; return 1;
    }
    case 0x09: {
        pthread_mutex_lock(&lock);
        Thread *t = get((uint32_t)r[0]);
        if (!t || t->started) hy_fail("invalid thread start");
        t->started = 1;
        pthread_mutex_unlock(&lock);
        if (pthread_create(&t->pthread, NULL, run, t)) hy_fail("pthread_create");
        r[0] = 0; return 1;
    }
    case 0x0a: hy_thread_returned();
    case 0x0b: {
        int64_t ns = (int64_t)r[0];
        if (ns > 0) { struct timespec t = {ns / 1000000000, ns % 1000000000}; nanosleep(&t, NULL); }
        else sched_yield();
        return 1;
    }
    case 0x0c: case 0x25: {
        pthread_mutex_lock(&lock);
        Thread *t = get((uint32_t)r[1]); if (!t) hy_fail("unknown thread handle");
        r[1] = number == 0x0c ? (uint64_t)t->priority : t->handle;
        pthread_mutex_unlock(&lock); r[0] = 0; return 1;
    }
    case 0x0d: {
        pthread_mutex_lock(&lock);
        Thread *t = get((uint32_t)r[0]); if (!t) hy_fail("unknown thread handle");
        t->priority = (int)r[1]; pthread_mutex_unlock(&lock); r[0] = 0; return 1;
    }
    case 0x0e: {
        pthread_mutex_lock(&lock);
        Thread *t = get((uint32_t)r[2]); if (!t) hy_fail("unknown thread handle");
        r[0] = 0; r[1] = (uint32_t)t->core; r[2] = t->core_mask;
        pthread_mutex_unlock(&lock); return 1;
    }
    case 0x0f: {
        pthread_mutex_lock(&lock);
        Thread *t = get((uint32_t)r[0]); if (!t) hy_fail("unknown thread handle");
        int core = (int)r[1];
        if (core >= 0 && core < 3) t->core = core;
        else if (core == -1) t->core = 0;
        else if (core != -2 && core != -3) hy_fail("invalid ideal core");
        if (r[2] && !(r[2] & ~7ull)) t->core_mask = r[2];
        else hy_fail("invalid core mask");
        pthread_mutex_unlock(&lock); r[0] = 0; return 1;
    }
    case 0x10: {
        pthread_mutex_lock(&lock); Thread *t = pthread_getspecific(current_key);
        r[0] = t->core < 0 ? 0 : t->core; pthread_mutex_unlock(&lock); return 1;
    }
    case 0x1a: {
        Thread *t = pthread_getspecific(current_key);
        _Atomic uint32_t *word = (_Atomic uint32_t *)(uintptr_t)r[1];
        pthread_mutex_lock(&lock);
        if (atomic_load(word) == ((uint32_t)r[0] | 0x40000000)) {
            t->wait_address = r[1]; t->wait_tag = r[2]; t->waiting = 1;
            while (t->waiting) pthread_cond_wait(&t->changed, &lock);
        }
        pthread_mutex_unlock(&lock); r[0] = 0; return 1;
    }
    case 0x1b: {
        pthread_mutex_lock(&lock); unlock_address(r[0]);
        pthread_mutex_unlock(&lock); r[0]=0; return 1;
    }
    case 0x1c: {
        Thread *t=pthread_getspecific(current_key);
        if (!t || (r[0]&3) || (r[1]&3)) hy_fail("invalid condition wait");
        pthread_mutex_lock(&lock);
        atomic_store((_Atomic uint32_t *)(uintptr_t)r[1],1);
        unlock_address(r[0]);
        uint32_t result=0;
        int64_t ns=(int64_t)r[3];
        if (!ns) result=0xea01;
        else {
            t->wait_address=r[0]; t->condition_key=r[1]; t->wait_tag=r[2];
            t->wait_result=0; t->waiting=3;
            struct timespec deadline; clock_gettime(CLOCK_MONOTONIC,&deadline);
            if (ns>0) { deadline.tv_sec+=ns/1000000000; deadline.tv_nsec+=ns%1000000000;
                deadline.tv_sec+=deadline.tv_nsec/1000000000; deadline.tv_nsec%=1000000000; }
            while (t->waiting) {
                int code;
                if (ns<0) code=pthread_cond_wait(&t->changed,&lock);
                else {
                    struct timespec now, remaining; clock_gettime(CLOCK_MONOTONIC,&now);
                    remaining.tv_sec=deadline.tv_sec-now.tv_sec; remaining.tv_nsec=deadline.tv_nsec-now.tv_nsec;
                    if (remaining.tv_nsec<0) { remaining.tv_sec--; remaining.tv_nsec+=1000000000; }
                    code=remaining.tv_sec<0 ? ETIMEDOUT : pthread_cond_timedwait_relative_np(&t->changed,&lock,&remaining);
                }
                if (code==ETIMEDOUT && t->waiting) { t->waiting=0; t->condition_key=0; t->wait_result=0xea01; }
                else if (code && code!=ETIMEDOUT) hy_fail("condition wait failed");
            }
            result=t->wait_result;
        }
        pthread_mutex_unlock(&lock); r[0]=result; return 1;
    }
    case 0x1d: {
        uintptr_t key=r[0]; int count=(int)r[1];
        if (key&3) hy_fail("unaligned condition key");
        pthread_mutex_lock(&lock);
        for (int sent=0;count<=0 || sent<count;sent++) {
            Thread *next=NULL;
            for (size_t i=0;i<64;i++) if (threads[i] && threads[i]->waiting==3 && threads[i]->condition_key==key)
                if (!next || threads[i]->priority<next->priority) next=threads[i];
            if (!next) break;
            next->condition_key=0;
            _Atomic uint32_t *word=(_Atomic uint32_t *)next->wait_address;
            uint32_t previous=atomic_load(word);
            for (;;) {
                uint32_t value=previous ? previous|0x40000000 : next->wait_tag;
                if (atomic_compare_exchange_weak(word,&previous,value)) break;
            }
            if (!previous) { next->waiting=0; pthread_cond_signal(&next->changed); }
            else {
                if (!get(previous&~0x40000000u)) hy_fail("condition mutex owner is invalid");
                next->waiting=1;
            }
        }
        int remaining=0;
        for (size_t i=0;i<64;i++) if (threads[i] && threads[i]->waiting==3 && threads[i]->condition_key==key) remaining=1;
        if (!remaining) atomic_store((_Atomic uint32_t *)key,0);
        pthread_mutex_unlock(&lock); r[0]=0; return 1;
    }
    case 0x34: {
        Thread *t = pthread_getspecific(current_key);
        _Atomic uint32_t *word = (_Atomic uint32_t *)(uintptr_t)r[0];
        if (r[1] > 2) hy_fail("unsupported address arbitration type");
        pthread_mutex_lock(&lock);
        uint32_t value = atomic_load(word), expected = (uint32_t)r[2];
        int ready = r[1] == 2 ? value == expected : (int32_t)value < (int32_t)expected;
        if (ready && r[1] == 1) {
            while (!atomic_compare_exchange_weak(word, &value, value - 1))
                if ((int32_t)value >= (int32_t)expected) { ready = 0; break; }
        }
        uint32_t result = 0;
        if (!ready) result = 0xfa01;
        else if (!r[3]) result = 0xea01;
        else {
            t->wait_address = r[0]; t->waiting = 2;
            int64_t ns = (int64_t)r[3]; struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            if (ns > 0) { deadline.tv_sec += ns / 1000000000; deadline.tv_nsec += ns % 1000000000;
                deadline.tv_sec += deadline.tv_nsec / 1000000000; deadline.tv_nsec %= 1000000000; }
            while (t->waiting) {
                int code = ns < 0 ? pthread_cond_wait(&t->changed, &lock) : pthread_cond_timedwait(&t->changed, &lock, &deadline);
                if (code == ETIMEDOUT && t->waiting) { t->waiting = 0; result = 0xea01; }
                else if (code && code != ETIMEDOUT) hy_fail("address wait failed");
            }
        }
        pthread_mutex_unlock(&lock); r[0] = result; return 1;
    }
    case 0x35: {
        if (r[1] > 2) hy_fail("unsupported address signal type");
        pthread_mutex_lock(&lock);
        unsigned waiting = 0;
        for (size_t i = 0; i < 64; i++) if (threads[i] && threads[i]->waiting == 2 && threads[i]->wait_address == r[0]) waiting++;
        uint32_t result = 0, expected = r[2]; int count = (int)r[3];
        if (r[1]) {
            uint32_t next = expected + 1;
            if (r[1] == 2 && waiting) next = count <= 0 || waiting <= (unsigned)count ? expected - 1 : expected;
            if (!atomic_compare_exchange_strong((_Atomic uint32_t *)(uintptr_t)r[0], &expected, next)) result = 0xfa01;
        }
        if (!result) {
            unsigned sent = 0;
            for (size_t i = 0; i < 64 && (count <= 0 || sent < (unsigned)count); i++)
                if (threads[i] && threads[i]->waiting == 2 && threads[i]->wait_address == r[0]) {
                    threads[i]->waiting = 0; pthread_cond_signal(&threads[i]->changed); sent++;
                }
        }
        pthread_mutex_unlock(&lock); r[0] = result; return 1;
    }
    default: return 0;
    }
}
