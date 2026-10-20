#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>
#include <TargetConditionals.h>

HyModule hy_modules[4];
size_t hy_module_count;
pthread_key_t hy_keys[8];

_Noreturn void hy_fail(const char *format, ...) {
    va_list args; va_start(args, format);
    fputs("STOP: ", stderr); vfprintf(stderr, format, args); fputc('\n', stderr);
    va_end(args); exit(42);
}
void *hy_range(HyModule *m, uint64_t offset, size_t length) {
    if (offset > m->size || length > m->size - offset)
        hy_fail("%s: image range out of bounds", m->name);
    return m->base + offset;
}
static FILE *open_file(const char *dir, const char *name, const char *suffix) {
    char path[4096];
    if (snprintf(path, sizeof path, "%s/%s.%s", dir, name, suffix) >= (int)sizeof path)
        hy_fail("path too long");
    FILE *f = fopen(path, "rb");
    if (!f) hy_fail("cannot open %s.%s", name, suffix);
    return f;
}
static void map_module(HyModule *m, const char *dir) {
    FILE *f = open_file(dir, m->name, "fixups.tsv");
    char line[256], key[64]; unsigned long long value;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "# %63s %llx", key, &value) != 2) continue;
        if (!strcmp(key, "TEXT_END")) m->text_end = value;
        if (!strcmp(key, "PATCH_OFF")) m->patch = value;
        if (!strcmp(key, "KEY_PAGE_OFF")) m->key_page = value;
    }
    fclose(f);
#if TARGET_OS_IPHONE
    extern void hy_map_signed_image(HyModule *module);
    hy_map_signed_image(m);
#else
    f = open_file(dir, m->name, "patched.bin");
    if (fseek(f, 0, SEEK_END)) hy_fail("image seek");
    long length = ftell(f);
    if (length < 16 || length > 512 * 1024 * 1024) hy_fail("invalid module size");
    rewind(f); m->size = ((size_t)length + 0x3fff) & ~(size_t)0x3fff;
    m->base = mmap(NULL, m->size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m->base == MAP_FAILED) hy_fail("image allocation");
    if (fread(m->base, 1, length, f) != (size_t)length) hy_fail("image read");
    fclose(f);
#endif
    if (!m->text_end || m->text_end > m->key_page || m->key_page + 0x4000 > m->patch ||
        m->patch >= m->size || (m->patch & 0x3fff) || (m->key_page & 0x3fff))
        hy_fail("%s: invalid section layout", m->name);
    uint64_t *page = hy_range(m, m->key_page, 64);
    for (int i = 0; i < 8; i++) page[i] = hy_keys[i] * 8;
#if !TARGET_OS_IPHONE
    // Round up to include every instruction; relocations must remain outside RX pages.
    size_t text_pages = (m->text_end + 0x3fff) & ~(size_t)0x3fff;
    if (text_pages && mprotect(m->base, text_pages, PROT_READ | PROT_EXEC)) hy_fail("text protection");
    if (mprotect(m->base + m->patch, m->size - m->patch, PROT_READ | PROT_EXEC)) hy_fail("patch protection");
    sys_icache_invalidate(m->base, m->size);
#endif
    f = open_file(dir, m->name, "gates.tsv");
    unsigned long long offset, original; unsigned arg; char kind[32];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%llx %31s %u %llx", &offset, kind, &arg, &original) != 4)
            hy_fail("invalid gate row");
        hy_range(m, offset, 1); hy_range(m, original, 4);
        hy_register_gate((uintptr_t)m->base + offset, kind, arg, (uintptr_t)m->base + original);
    }
    fclose(f);
}
// Built once before relocation; read-only during concurrent game/audio lookups.
static int compare_symbols(void *context,const void *a,const void *b) {
    HyModule *m=context;HySymbol *left=*(HySymbol *const *)a,*right=*(HySymbol *const *)b;
    int order=strcmp(m->strings+left->name,m->strings+right->name);
    return order ? order : (left>right)-(left<right);
}
static void index_symbols(HyModule *m) {
    m->symbol_index=calloc(m->symbol_count,sizeof *m->symbol_index);
    if(!m->symbol_index)hy_fail("symbol index allocation");
    m->export_count=0;
    for(size_t i=1;i<m->symbol_count;i++)if(m->symbols[i].section)
        m->symbol_index[m->export_count++]=&m->symbols[i];
    qsort_r(m->symbol_index,m->export_count,sizeof *m->symbol_index,m,compare_symbols);
}
static void dynamic_info(HyModule *m) {
    uint32_t mod; memcpy(&mod, m->base + 4, 4);
    uint8_t *header = hy_range(m, mod, 8);
    if (memcmp(header, "MOD0", 4)) hy_fail("%s: missing MOD0", m->name);
    int32_t relative; memcpy(&relative, header + 4, 4);
    int64_t off = (int64_t)mod + relative;
    for (;;) {
        uint64_t *entry = hy_range(m, off, 16); off += 16;
        if (!entry[0]) break;
        if (entry[0] < 32) m->dynamic[entry[0]] = entry[1];
    }
    uint32_t *hash = hy_range(m, m->dynamic[4], 8);
    m->symbol_count = hash[1];
    if (!m->symbol_count || m->symbol_count > m->size / sizeof(HySymbol)) hy_fail("symbol count");
    m->symbols = hy_range(m, m->dynamic[6], m->symbol_count * sizeof(HySymbol));
    m->strings = hy_range(m, m->dynamic[5], m->dynamic[10]);
    for (size_t i = 0; i < m->symbol_count; i++) {
        unsigned n = m->symbols[i].name;
        if (n >= m->dynamic[10] || !memchr(m->strings + n, 0, m->dynamic[10] - n)) hy_fail("invalid symbol name");
    }
    index_symbols(m);
}
void *hy_symbol(const char *name) {
    for (size_t j = 0; j < hy_module_count; j++) {
        HyModule *m = &hy_modules[j];
        size_t low=0,high=m->export_count;
        while(low<high) {
            size_t middle=low+(high-low)/2;
            if(strcmp(m->strings+m->symbol_index[middle]->name,name)<0)low=middle+1;
            else high=middle;
        }
        if(low<m->export_count) {
            HySymbol *s=m->symbol_index[low];
            if(!strcmp(m->strings+s->name,name)) {
                if(s->section==0xfff1)return (void *)(uintptr_t)s->value;
                return hy_range(m,s->value,s->size);
            }
        }
    }
    return NULL;
}
static size_t relocate(HyModule *m, uint64_t offset, uint64_t size) {
    if (size % sizeof(HyRela)) hy_fail("invalid relocation size");
    HyRela *entries = hy_range(m, offset, size);
    for (size_t i = 0; i < size / sizeof(HyRela); i++) {
        HyRela *r = &entries[i];
        uint32_t type = (uint32_t)r->info, symbol = (uint32_t)(r->info >> 32);
        uint64_t value;
        if (type == 1027) value = (uintptr_t)m->base + r->addend;
        else if (type == 257 || type == 1025 || type == 1026) {
            if (symbol >= m->symbol_count) hy_fail("relocation symbol index");
            HySymbol *s = &m->symbols[symbol];
            const char *name = m->strings + s->name;
            void *p = hy_service(name);
            if (!p) p = hy_symbol(name);
            if (!p && s->section) p = hy_range(m, s->value, s->size);
            if (!p && (s->info >> 4) != 2) hy_fail("%s: unresolved strong import %s", m->name, name);
            value = (uintptr_t)p + r->addend;
        } else if (type == 0) continue;
        else hy_fail("%s: unsupported relocation %u", m->name, type);
        if (r->offset < ((m->text_end + 0x3fff) & ~(size_t)0x3fff) ||
            r->offset > m->key_page - 8) hy_fail("relocation into code or keys");
        memcpy(hy_range(m, r->offset, 8), &value, 8);
    }
    return size / sizeof(HyRela);
}
void hy_load(const char *dir, int fixture) {
    // This loader replaces rtld; rtld's own code is not executed or relocated.
    const char *names[] = { "main", "subsdk0", "sdk" };
    for (int i = 0; i < 8; i++) if (pthread_key_create(&hy_keys[i], NULL)) hy_fail("pthread key creation");
    hy_module_count = fixture ? 1 : 3;
    for (size_t i = 0; i < hy_module_count; i++) {
        hy_modules[i].name = fixture ? "fixture" : names[i];
        map_module(&hy_modules[i], dir); dynamic_info(&hy_modules[i]);
    }
    for (size_t i = 0; i < hy_module_count; i++) {
        HyModule *m = &hy_modules[i];
        size_t count = relocate(m, m->dynamic[7], m->dynamic[8]);
        count += relocate(m, m->dynamic[23], m->dynamic[2]);
        fprintf(stderr, "loaded %s: %zu symbols, %zu relocations\n", m->name, m->symbol_count, count);
    }
}
