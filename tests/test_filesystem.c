#include "../src/runtime/runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
_Noreturn void hy_fail(const char *fmt, ...) { (void)fmt; abort(); }
int main(int argc, char **argv) {
    assert(argc == 2); hy_filesystem_init(argv[1]);
    unsigned (*mount_rom)(const char *, void *, size_t) = hy_filesystem_service("_ZN2nn2fs8MountRomEPKcPvm");
    unsigned (*open_file)(uint64_t *, const char *, int) = hy_filesystem_service("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
    unsigned (*read_file)(size_t *, uint64_t, int64_t, void *, size_t) = hy_filesystem_service("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
    void (*close_file)(uint64_t) = hy_filesystem_service("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
    assert(mount_rom && open_file && read_file && close_file);
    assert(mount_rom("rom", NULL, 0) == 0);
    uint64_t h = 0; assert(open_file(&h, "rom:/data.bin", 1) == 0);
    char bytes[8] = {0}; size_t got = 0;
    assert(read_file(&got, h, 1, bytes, 7) == 0 && got == 3 && !memcmp(bytes, "bcd", 3));
    assert(read_file(&got, h, -1, bytes, 4) != 0);
    close_file(h);
    assert(read_file(&got, h, 0, bytes, 4) != 0);
    assert(open_file(&h, "rom:/../outside.bin", 1) != 0);
    assert(open_file(&h, "rom:/escape", 1) != 0);
    assert(open_file(&h, "rom:/data.bin", 2) != 0);
    puts("PASS: RomFS reads, EOF, stale handles and path containment");
}
