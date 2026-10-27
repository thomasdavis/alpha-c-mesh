/* shm.rs: memfd_create (Linux) / shm_open + unlink (macOS), ftruncate, mmap. */
#include "mesh/shm.h"
#include "mesh/text.h"

#include "mesh/abi/base.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static char *os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

static int create(char **err) {
#if defined(__linux__)
    int fd = memfd_create("meshd-mailbox", MFD_CLOEXEC);
    if (fd < 0) {
        char *e = os_error(errno);
        mesh_errf(err, "mailbox: memfd_create: %s", e);
        free(e);
    }
    return fd;
#else
    static atomic_uint n;
    /* Names are at most 31 bytes on macOS (PSHMNAMLEN). */
    char name[32];
    snprintf(name, sizeof name, "/meshd.%d.%u", (int)getpid(), atomic_fetch_add(&n, 1));
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        char *e = os_error(errno);
        mesh_errf(err, "mailbox: shm_open \"%s\": %s", name, e);
        free(e);
        return -1;
    }
    shm_unlink(name);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
#endif
}

bool mesh_shm_with_len(size_t len, mesh_shm *out, char **err) {
    int fd = create(err);
    if (fd < 0) return false;
    if (ftruncate(fd, (off_t)len) != 0) {
        char *e = os_error(errno);
        close(fd);
        mesh_errf(err, "mailbox: ftruncate %zu: %s", len, e);
        free(e);
        return false;
    }
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        char *e = os_error(errno);
        close(fd);
        mesh_errf(err, "mailbox: mmap: %s", e);
        free(e);
        return false;
    }
    *out = (mesh_shm){p, len, fd};
    return true;
}

bool mesh_shm_new(mesh_shm *out, char **err) { return mesh_shm_with_len(MESH_MAILBOX_BYTES, out, err); }

char *mesh_shm_path(const mesh_shm *s) { return mesh_fmt("/proc/%d/fd/%d", (int)getpid(), s->fd); }

void mesh_shm_close(mesh_shm *s) {
    if (s->base) munmap(s->base, s->len);
    if (s->fd >= 0) close(s->fd);
    *s = (mesh_shm){NULL, 0, -1};
}
