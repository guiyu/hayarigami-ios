#include "psfs.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char table[80] = {0};
    memcpy(table, "PS_FS_V1", 8);
    table[8] = 1;
    memcpy(table + 16, "test.nltx", 10);
    table[64] = 5;
    table[73] = 2; /* little endian 0x200 */
    uint32_t count = 0;
    psfs_entry entry;
    assert(psfs_count(table, sizeof table, &count) == 0 && count == 1);
    assert(psfs_get(table, sizeof table, 517, 0, &entry) == 0);
    assert(strcmp(entry.name, "test.nltx") == 0);
    assert(entry.offset == 512 && entry.size == 5);
    assert(psfs_get(table, sizeof table, 516, 0, &entry) != 0);
    assert(psfs_get(table, sizeof table, 517, 1, &entry) != 0);
    assert(psfs_get(table, sizeof table - 1, 517, 0, &entry) != 0);
    memset(table + 16, 'x', 48);
    assert(psfs_get(table, sizeof table, 517, 0, &entry) != 0);
    memcpy(table + 16, "../bad\0", 7);
    assert(psfs_get(table, sizeof table, 517, 0, &entry) != 0);
    memcpy(table + 16, "ok\0", 3);
    memset(table + 64, 255, 16); /* offset + size overflow */
    assert(psfs_get(table, sizeof table, UINT64_MAX, 0, &entry) != 0);
    table[0] = 'X';
    assert(psfs_count(table, sizeof table, &count) != 0);
    puts("PS_FS_V1 bounds tests passed");
    return 0;
}
