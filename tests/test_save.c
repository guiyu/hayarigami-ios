#define _DARWIN_C_SOURCE
#include "../src/runtime/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
_Noreturn void hy_fail(const char *format,...) {(void)format;abort();}
int main(int argc,char **argv) {
 assert(argc==2);setenv("HY_SAVE_DIR",argv[1],1);uint64_t user[2]={0},handle=0;
 unsigned (*select_user)(void *,const void *)=hy_account_service("_ZN2nn7account16ShowUserSelectorEPNS0_3UidERKNS0_21UserSelectionSettingsE");assert(!select_user(user,NULL));
 unsigned (*ensure)(const void *)=hy_filesystem_service("_ZN2nn2fs14EnsureSaveDataERKNS_7account3UidE");
 unsigned (*mount)(const char *,const void *)=hy_filesystem_service("_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE");
 unsigned (*create)(const char *,int64_t)=hy_filesystem_service("_ZN2nn2fs10CreateFileEPKcl");
 unsigned (*open_file)(uint64_t *,const char *,int)=hy_filesystem_service("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
 unsigned (*write_file)(uint64_t,int64_t,const void *,size_t,const uint32_t *)=hy_filesystem_service("_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE");
 unsigned (*read_file)(size_t *,uint64_t,int64_t,void *,size_t)=hy_filesystem_service("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
 unsigned (*commit)(const char *)=hy_filesystem_service("_ZN2nn2fs14CommitSaveDataEPKc");
 void (*close_file)(uint64_t)=hy_filesystem_service("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
 assert(ensure && mount && create && open_file && write_file && read_file && commit && close_file);
 uint64_t invalid[2]={0};assert(ensure(invalid));assert(!ensure(user));assert(mount("../escape",user));assert(!mount("save",user));
 assert(!create("save:/system.bin",8));assert(create("save:/system.bin",8));assert(create("save:/../escape.bin",8));assert(open_file(&handle,"save:/escape",2));
 assert(!open_file(&handle,"save:/system.bin",3));uint32_t option=1;assert(!write_file(handle,2,"ABCD",4,&option));assert(write_file(handle,7,"XX",2,&option));assert(write_file(handle,-1,"X",1,&option));
 close_file(handle);assert(!commit("save"));assert(!open_file(&handle,"save:/system.bin",1));char bytes[8]={0};size_t got=0;
 assert(!read_file(&got,handle,0,bytes,8) && got==8 && !memcmp(bytes,(char[]){0,0,'A','B','C','D',0,0},8));
 assert(write_file(handle,0,"X",1,&option));close_file(handle);assert(read_file(&got,handle,0,bytes,8));
 puts("PASS: isolated save UID/mount, create/write/commit/reopen, bounds, stale/read-only handles and symlink escape rejection");
}
