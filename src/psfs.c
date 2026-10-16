#include "psfs.h"
#include <string.h>

static uint64_t le(const unsigned char *p, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

int psfs_count(const void *table, size_t bytes, uint32_t *count) {
    if (!table || !count || bytes < 16 || memcmp(table, "PS_FS_V1", 8)) return -1;
    const unsigned char *p = table;
    uint32_t n = (uint32_t)le(p + 8, 4);
    if ((uint64_t)n * 64 + 16 > bytes) return -1;
    *count = n;
    return 0;
}

int psfs_get(const void *table, size_t bytes, uint64_t archive_size,
             uint32_t index, psfs_entry *entry) {
    uint32_t count;
    if (!entry || psfs_count(table, bytes, &count) || index >= count) return -1;
    const unsigned char *p = (const unsigned char *)table + 16 + (size_t)index * 64;
    const unsigned char *end = memchr(p, 0, 48);
    if (!end || end == p) return -1;
    size_t length = (size_t)(end - p);
    if (memchr(p, '/', length) || memchr(p, '\\', length) ||
        (length == 1 && p[0] == '.') ||
        (length == 2 && p[0] == '.' && p[1] == '.')) return -1;
    uint64_t size = le(p + 48, 8), offset = le(p + 56, 8);
    if (offset < (uint64_t)count * 64 + 16 || offset > archive_size ||
        size > archive_size - offset) return -1;
    memcpy(entry->name, p, length);
    entry->name[length] = 0;
    entry->offset = offset;
    entry->size = size;
    return 0;
}
