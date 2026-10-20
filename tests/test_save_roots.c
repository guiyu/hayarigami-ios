#define _DARWIN_C_SOURCE
#include "../src/runtime/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
_Noreturn void hy_fail(const char *s,...) {(void)s;abort();}
int main(int argc,char **argv) {
 assert(argc==3);hy_filesystem_init(argv[1]);setenv("HY_SAVE_DIR",argv[2],1);uint64_t uid[2];
 unsigned (*select)(void *,const void *)=hy_account_service("_ZN2nn7account16ShowUserSelectorEPNS0_3UidERKNS0_21UserSelectionSettingsE");assert(select && !select(uid,NULL));
 unsigned (*ensure)(const void *)=hy_filesystem_service("_ZN2nn2fs14EnsureSaveDataERKNS_7account3UidE");assert(ensure && ensure(uid));
 puts("PASS: overlapping RomFS and SaveFS roots rejected");
}
