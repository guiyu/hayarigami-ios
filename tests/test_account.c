#include "../src/runtime/account.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format,...) {(void)format;abort();}
int main(void) {
    initialize_account();
    struct { LocalUserHandle handle; uint64_t guard; } object={.guard=0x123456789abcdef};
    assert(preselected_user(&object.handle));LocalUserId uid;
    assert(!get_user_id(&uid,&object.handle) && hy_account_user_valid(&uid));
    char nickname[34];memset(nickname,0x69,sizeof nickname);assert(!get_nickname(nickname,&uid));
    assert(!strcmp(nickname,"Local Player") && nickname[33]==0x69);
    LocalUserId selected;assert(!select_user(&selected,NULL) && !memcmp(&selected,&uid,sizeof uid));
    assert(!open_user(&object.handle,&uid));assert(object.guard==0x123456789abcdef);
    uid.low^=1;assert(!hy_account_user_valid(&uid) && open_user(&object.handle,&uid));
    assert(hy_account_service("_ZN2nn7account10InitializeEv"));
    puts("PASS: isolated local user, stable UID, nickname bounds and invalid-user rejection");
}
