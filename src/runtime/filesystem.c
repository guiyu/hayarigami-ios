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
static char mount_name[16];
enum { FILE_LIMIT=128 }; // The title retains at least 66 archive handles during startup.
static struct { uint64_t handle; int fd; } files[FILE_LIMIT];
static uint64_t next_handle = 1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void hy_filesystem_init(const char *root) {
    root_fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root_fd < 0) hy_fail("cannot open local RomFS directory");
}
static unsigned result(void) { return errno == ENOENT ? 0x202 : 0xfa01; }
static int contained_open(const char *path) {
    size_t n = strlen(mount_name);
    if (!n || strncmp(path, mount_name, n) || path[n] != ':' || path[n + 1] != '/') { errno = EINVAL; return -1; }
    path += n + 2;
    if (!*path || strlen(path) >= PATH_MAX) { errno = EINVAL; return -1; }
    char copy[PATH_MAX]; strlcpy(copy, path, sizeof copy);
    int dir = dup(root_fd); if (dir < 0) return -1;
    char *part = copy;
    for (;;) {
        char *slash = strchr(part, '/'); if (slash) *slash = 0;
        if (!*part || !strcmp(part, ".") || !strcmp(part, "..") || strchr(part, '\\')) {
            close(dir); errno = EINVAL; return -1;
        }
        int fd = openat(dir, part, O_RDONLY | O_NOFOLLOW | (slash ? O_DIRECTORY : 0));
        int error = errno; close(dir); errno = error;
        if (fd < 0 || !slash) return fd;
        dir = fd; part = slash + 1;
    }
}
static unsigned cache_size(size_t *out) { *out = 0; return 0; }
static unsigned mount_rom(const char *name, void *cache, size_t size) {
    (void)cache; (void)size;
    if (root_fd < 0 || !*name || strlen(name) >= sizeof mount_name || strpbrk(name, ":/\\")) return 0xfa01;
    strlcpy(mount_name, name, sizeof mount_name); return 0;
}
static void unmount(const char *name) { if (!strcmp(name, mount_name)) mount_name[0] = 0; }
static unsigned open_file(uint64_t *out, const char *path, int mode) {
    *out = 0;
    if (mode != 1) return 0xfa01; // RomFS is always read-only.
    int fd = contained_open(path); if (fd < 0) return result();
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return 0xfa01; }
    pthread_mutex_lock(&lock);
    size_t i; for (i = 0; i < FILE_LIMIT && files[i].handle; i++) {}
    if (i == FILE_LIMIT) { pthread_mutex_unlock(&lock); close(fd); return 0xd401; }
    files[i].fd = fd; files[i].handle = next_handle++;
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
    int fd = fd_for(handle);
    if (fd < 0) { pthread_mutex_unlock(&lock); return 0xe401; }
    ssize_t count;
    do { count = pread(fd, buffer, size, offset); } while (count < 0 && errno == EINTR);
    unsigned rc = count < 0 ? result() : 0;
    if (count >= 0) *out = (size_t)count;
    pthread_mutex_unlock(&lock); return rc;
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
