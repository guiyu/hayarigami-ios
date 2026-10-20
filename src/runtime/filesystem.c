#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

static int root_fd = -1;
static char mount_name[16],save_mount[16];
static int save_fd=-1;
enum { FILE_LIMIT=128 }; // The title retains at least 66 archive handles during startup.
static struct { uint64_t handle; int fd,mode,save; } files[FILE_LIMIT];
static uint64_t next_handle = 1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void hy_filesystem_init(const char *root) {
    root_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root_fd < 0) hy_fail("cannot open local RomFS directory");
}
static unsigned result(void) { return errno==ENOENT ? 0x202 : errno==EEXIST ? 0x402 : 0xfa01; }
static int parent_for(const char *path,int writing,char leaf[NAME_MAX+1],int *is_save) {
    if(!path) {errno=EINVAL;return -1;}
    const char *colon=strchr(path,':');if(!colon || colon[1]!='/') {errno=EINVAL;return -1;}
    size_t n=(size_t)(colon-path);int root=-1;
    if(n && n==strlen(mount_name) && !memcmp(path,mount_name,n)) {root=root_fd;*is_save=0;}
    else if(n && n==strlen(save_mount) && !memcmp(path,save_mount,n)) {root=save_fd;*is_save=1;}
    if(root<0 || (writing && !*is_save)) {errno=EACCES;return -1;}
    path=colon+2;if(!*path || strlen(path)>=PATH_MAX) {errno=EINVAL;return -1;}
    char copy[PATH_MAX];strlcpy(copy,path,sizeof copy);int dir=dup(root);if(dir<0)return -1;
    char *part=copy;
    for(;;) {
        char *slash=strchr(part,'/');if(slash)*slash=0;
        if(!*part || strlen(part)>NAME_MAX || !strcmp(part,".") || !strcmp(part,"..") || strchr(part,'\\')) {close(dir);errno=EINVAL;return -1;}
        if(!slash) {strlcpy(leaf,part,NAME_MAX+1);return dir;}
        int next=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);int error=errno;close(dir);errno=error;
        if(next<0)return -1;dir=next;part=slash+1;
    }
}
static int contained_open_mode(const char *path,int flags,int writing,int *is_save) {
    char leaf[NAME_MAX+1];int parent=parent_for(path,writing,leaf,is_save);if(parent<0)return -1;
    int fd=openat(parent,leaf,flags|O_NOFOLLOW,0600);int error=errno;close(parent);errno=error;return fd;
}
static int contained_open(const char *path) {int save=0;return contained_open_mode(path,O_RDONLY,0,&save);}
static int contains_root(int child,const struct stat *ancestor) {
    int current=dup(child);if(current<0)return -1;
    for(unsigned depth=0;depth<256;depth++) {
        struct stat here,parent_stat;if(fstat(current,&here)) {close(current);return -1;}
        if(here.st_dev==ancestor->st_dev && here.st_ino==ancestor->st_ino) {close(current);return 1;}
        int parent=openat(current,"..",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
        if(parent<0 || fstat(parent,&parent_stat)) {if(parent>=0)close(parent);close(current);return -1;}
        close(current);
        if(parent_stat.st_dev==here.st_dev && parent_stat.st_ino==here.st_ino) {close(parent);return 0;}
        current=parent;
    }
    close(current);return -1;
}
static unsigned ensure_save(const void *uid) {
    if(!hy_account_user_valid(uid))return 0xfa01;
    pthread_mutex_lock(&lock);
    if(save_fd<0) {
        const char *path=getenv("HY_SAVE_DIR");
        if(!path || !*path)hy_fail("independent save directory requires HY_SAVE_DIR");
        int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0) {pthread_mutex_unlock(&lock);return result();}
        struct stat save,rom;
        if(fstat(fd,&save) || (root_fd>=0 && (fstat(root_fd,&rom) || contains_root(fd,&rom)!=0 || contains_root(root_fd,&save)!=0))) {
            close(fd);pthread_mutex_unlock(&lock);return 0xfa01;
        }
        save_fd=fd;fputs("host save: isolated local directory\n",stderr);
    }
    pthread_mutex_unlock(&lock);return 0;
}
static unsigned mount_save(const char *name,const void *uid) {
    unsigned rc=ensure_save(uid);if(rc)return rc;
    if(!name || !*name || strlen(name)>=sizeof save_mount || strpbrk(name,":/\\") || !strcmp(name,mount_name))return 0xfa01;
    strlcpy(save_mount,name,sizeof save_mount);return 0;
}
static unsigned create_file(const char *path,int64_t size) {
    if(size<0)return 0xfa01;int save=0;char leaf[NAME_MAX+1];int parent=parent_for(path,1,leaf,&save);if(parent<0)return result();
    int fd=openat(parent,leaf,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);unsigned rc=0;
    if(fd<0)rc=result();else {if(ftruncate(fd,size) || fsync(fd)) {rc=result();unlinkat(parent,leaf,0);}close(fd);}
    close(parent);return rc;
}
static unsigned delete_file(const char *path) {
    int save=0;char leaf[NAME_MAX+1];int parent=parent_for(path,1,leaf,&save);if(parent<0)return result();
    struct stat st;unsigned rc=fstatat(parent,leaf,&st,AT_SYMLINK_NOFOLLOW) ? result() : !S_ISREG(st.st_mode) ? 0xfa01 : unlinkat(parent,leaf,0) ? result() : 0;
    close(parent);return rc;
}
static unsigned cache_size(size_t *out) { *out = 0; return 0; }
static unsigned mount_rom(const char *name, void *cache, size_t size) {
    (void)cache; (void)size;
    if (root_fd < 0 || !*name || strlen(name) >= sizeof mount_name || strpbrk(name, ":/\\")) return 0xfa01;
    strlcpy(mount_name, name, sizeof mount_name); return 0;
}
static void unmount(const char *name) { if (!strcmp(name,mount_name))mount_name[0]=0;else if(!strcmp(name,save_mount))save_mount[0]=0; }
static unsigned open_file(uint64_t *out, const char *path, int mode) {
    *out = 0;
    if((mode&~7) || !(mode&3))return 0xfa01;
    int save=0,flags=(mode&3)==3 ? O_RDWR : (mode&2) ? O_WRONLY : O_RDONLY;
    int fd=contained_open_mode(path,flags,(mode&2)!=0,&save); if (fd < 0) return result();
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return 0xfa01; }
    pthread_mutex_lock(&lock);
    size_t i; for (i = 0; i < FILE_LIMIT && files[i].handle; i++) {}
    if (i == FILE_LIMIT) { pthread_mutex_unlock(&lock); close(fd); return 0xd401; }
    files[i].fd=fd;files[i].mode=mode;files[i].save=save;files[i].handle = next_handle++;
    *out = files[i].handle; pthread_mutex_unlock(&lock);
    fprintf(stderr, "open resource: %s (%lld bytes)\n", path, (long long)st.st_size);
    return 0;
}
static int fd_for(uint64_t handle) {
    if (handle) for (size_t i = 0; i < FILE_LIMIT; i++) if (files[i].handle == handle) return files[i].fd;
    return -1;
}
static unsigned read_file(size_t *out, uint64_t handle, int64_t offset, void *buffer, size_t size) {
    *out = 0; if (offset < 0 || size > SSIZE_MAX) return 0xfa01;
    pthread_mutex_lock(&lock);
    int fd = fd_for(handle);int readable=0;for(size_t i=0;i<FILE_LIMIT;i++)if(files[i].handle==handle && (files[i].mode&1))readable=1;
    if (fd < 0 || !readable) { pthread_mutex_unlock(&lock); return 0xe401; }
    ssize_t count;
    do { count = pread(fd, buffer, size, offset); } while (count < 0 && errno == EINTR);
    unsigned rc = count < 0 ? result() : 0;
    if (count >= 0) *out = (size_t)count;
    pthread_mutex_unlock(&lock); return rc;
}
static unsigned write_file(uint64_t handle,int64_t offset,const void *data,size_t size,const uint32_t *option) {
    if(offset<0 || size>SSIZE_MAX || !option || (*option&~1u))return 0xfa01;
    pthread_mutex_lock(&lock);size_t i;
    for(i=0;i<FILE_LIMIT && files[i].handle!=handle;i++){}
    if(!handle || i==FILE_LIMIT || !files[i].save || !(files[i].mode&2)) {pthread_mutex_unlock(&lock);return 0xe401;}
    int fd=files[i].fd;struct stat st;unsigned rc=0;
    if(fstat(fd,&st))rc=result();
    else if(offset>INT64_MAX-(int64_t)size || (!(files[i].mode&4) && (offset>st.st_size || size>(uint64_t)(st.st_size-offset))))rc=0xfa01;
    size_t done=0;
    while(!rc && done<size) {
        ssize_t n=pwrite(fd,(const uint8_t *)data+done,size-done,offset+(int64_t)done);
        if(n<0 && errno==EINTR)continue;if(n<=0) {rc=n<0 ? result() : 0xfa01;break;}done+=(size_t)n;
    }
    if(!rc && (*option&1) && fsync(fd))rc=result();pthread_mutex_unlock(&lock);return rc;
}
static unsigned commit_save(const char *name) {
    if(!name || !*save_mount || strcmp(name,save_mount))return 0xfa01;
    pthread_mutex_lock(&lock);unsigned rc=0;
    for(size_t i=0;i<FILE_LIMIT && !rc;i++)if(files[i].handle && files[i].save && (files[i].mode&2) && fsync(files[i].fd))rc=result();
    if(!rc && fsync(save_fd))rc=result();pthread_mutex_unlock(&lock);return rc;
}
static unsigned file_size(int64_t *out, uint64_t handle) {
    pthread_mutex_lock(&lock); int fd = fd_for(handle); struct stat st;
    unsigned rc = fd < 0 ? 0xe401 : fstat(fd, &st) ? result() : 0;
    if (!rc) *out = st.st_size;
    pthread_mutex_unlock(&lock); return rc;
}
static void close_file(uint64_t handle) {
    pthread_mutex_lock(&lock);
    if (handle) for (size_t i = 0; i < FILE_LIMIT; i++) if (files[i].handle == handle) {
        if(files[i].save && (files[i].mode&2) && fsync(files[i].fd))hy_fail("save close flush failed");
        close(files[i].fd); files[i].handle = 0; break;
    }
    pthread_mutex_unlock(&lock);
}
static unsigned entry_type(int *out, const char *path) {
    int fd = contained_open(path); if (fd < 0) return result();
    struct stat st; int rc = fstat(fd, &st); close(fd);
    if (rc) return 0xfa01;
    *out = S_ISDIR(st.st_mode) ? 0 : 1; return 0;
}
void *hy_filesystem_service(const char *name) {
#define ENTRY(n, f) {n, (void *)f}
    static const struct { const char *name; void *fn; } table[] = {
        ENTRY("_ZN2nn2fs14EnsureSaveDataERKNS_7account3UidE",ensure_save),
        ENTRY("_ZN2nn2fs13MountSaveDataEPKcRKNS_7account3UidE",mount_save),
        ENTRY("_ZN2nn2fs14CommitSaveDataEPKc",commit_save),
        ENTRY("_ZN2nn2fs10CreateFileEPKcl",create_file),
        ENTRY("_ZN2nn2fs10DeleteFileEPKc",delete_file),
        ENTRY("_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE",write_file),
        ENTRY("_ZN2nn2fs22QueryMountRomCacheSizeEPm", cache_size),
        ENTRY("_ZN2nn2fs8MountRomEPKcPvm", mount_rom),
        ENTRY("_ZN2nn2fs7UnmountEPKc", unmount),
        ENTRY("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci", open_file),
        ENTRY("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm", read_file),
        ENTRY("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE", file_size),
        ENTRY("_ZN2nn2fs9CloseFileENS0_10FileHandleE", close_file),
        ENTRY("_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc", entry_type),
    };
#undef ENTRY
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) if (!strcmp(name, table[i].name)) return table[i].fn;
    return NULL;
}
