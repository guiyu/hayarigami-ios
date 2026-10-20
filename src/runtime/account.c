#include "runtime.h"
#include <stdio.h>
#include <string.h>
typedef struct { uint64_t low,high; } LocalUserId;
// Host-private token fits the guest opaque UserHandle. No Nintendo account is opened.
typedef struct { uint64_t token; } LocalUserHandle;
static const LocalUserId local_user={0x485941524947414dull,0x494c4f43414c0001ull};
static const uint64_t local_token=0x48594c4f43414c01ull;
static void initialize_account(void) { fputs("host account: isolated Local Player\n",stderr); }
int hy_account_user_valid(const void *uid) { return uid && !memcmp(uid,&local_user,sizeof local_user); }
static unsigned open_user(LocalUserHandle *out,const LocalUserId *uid) {
    out->token=0;
    if (!hy_account_user_valid(uid)) return 0xfa01;
    out->token=local_token; return 0;
}
static unsigned preselected_user(LocalUserHandle *out) { return open_user(out,&local_user)==0; }
static unsigned get_user_id(LocalUserId *out,const LocalUserHandle *handle) {
    memset(out,0,sizeof *out);
    if (handle->token!=local_token) return 0xfa01;
    *out=local_user; return 0;
}
static unsigned get_nickname(char *out,const LocalUserId *uid) {
    if (!hy_account_user_valid(uid)) return 0xfa01;
    memset(out,0,33);memcpy(out,"Local Player",12);return 0;
}
static unsigned select_user(LocalUserId *out,const void *settings) {
    (void)settings;*out=local_user;return 0;
}
void *hy_account_service(const char *name) {
    static const struct { const char *name;void *fn; } entries[]={
        {"_ZN2nn7account10InitializeEv",initialize_account},
        {"_ZN2nn7account22TryOpenPreselectedUserEPNS0_10UserHandleE",preselected_user},
        {"_ZN2nn7account8OpenUserEPNS0_10UserHandleERKNS0_3UidE",open_user},
        {"_ZN2nn7account9GetUserIdEPNS0_3UidERKNS0_10UserHandleE",get_user_id},
        {"_ZN2nn7account11GetNicknameEPNS0_8NicknameERKNS0_3UidE",get_nickname},
        {"_ZN2nn7account16ShowUserSelectorEPNS0_3UidERKNS0_21UserSelectionSettingsE",select_user},
    };
    for (size_t i=0;i<sizeof entries/sizeof entries[0];i++) if (!strcmp(name,entries[i].name)) return entries[i].fn;
    return NULL;
}
