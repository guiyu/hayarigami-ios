#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void callback_a(void) { fputs("SDK callback A reached\n", stderr); }
static void callback_b(void) {
    fputs("SDK initialized; invoking module constructors\n", stderr);
    for (size_t i = 0; i < hy_module_count; i++) {
        HyModule *m = &hy_modules[i];
        if (m->dynamic[12]) ((void (*)(void))hy_range(m, m->dynamic[12], 4))();
    }
}
static void callback_c(void) { fputs("game main returned\n", stderr); }
int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) { fputs("usage: hayarigami-host MODULE_DIR --fixture|--load-only|--boot [ROMFS_DIR]\n", stderr); return 2; }
    int fixture = !strcmp(argv[2], "--fixture");
    if (!fixture && strcmp(argv[2], "--load-only") && strcmp(argv[2], "--boot")) return 2;
    hy_install_signals(); alarm(30);
    hy_load(argv[1], fixture); hy_thread_init(); hy_thread_main();
    if (fixture) {
        uint64_t (*entry)(void) = hy_symbol("hy_fixture");
        if (!entry || entry() != 42) hy_fail("synthetic execution failed");
        puts("PASS: synthetic ARM64 module returned 42"); return 0;
    }
    if (!strcmp(argv[2], "--load-only")) { puts("PASS: all guest modules relocated"); return 0; }
    if (argc != 4) hy_fail("boot requires an extracted RomFS directory");
    hy_filesystem_init(argv[3]);
    void (*start)(uint64_t, uint64_t, void (*)(void), void (*)(void), void (*)(void)) =
        hy_symbol("_ZN2nn4init5StartEmmPFvvES2_S2_");
    if (!start) hy_fail("SDK Start export not found");
    fputs("entering real SDK Start\n", stderr);
    alarm(120); // Resource initialization can outlast the short loader/fixture guard.
    start(0x100, 0, callback_a, callback_b, callback_c);
    hy_fail("SDK Start unexpectedly returned");
}
