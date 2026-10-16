#ifndef HAYARIGAMI_PSFS_H
#define HAYARIGAMI_PSFS_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[49];
    uint64_t offset;
    uint64_t size;
} psfs_entry;

/* table is the 16-byte header followed by count * 64 bytes of entries.
 * archive_size is the complete archive's size; bytes may contain only its table.
 * Return 0 on success, -1 for malformed input or an out-of-range index. */
int psfs_count(const void *table, size_t bytes, uint32_t *count);
int psfs_get(const void *table, size_t bytes, uint64_t archive_size,
             uint32_t index, psfs_entry *entry);
#endif
