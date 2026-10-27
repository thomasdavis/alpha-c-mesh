/* shm.rs's tests: two mappings share words; a child's writes land in the parent's mapping. */
#include "mesh/check.h"
#include "mesh/shm.h"

#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    mesh_shm s;
    char *err = NULL;
    CHECK(mesh_shm_with_len(1 << 16, &s, &err));
    CHECK_U((uintptr_t)s.base % 4096, 0);
    int fd = dup(s.fd);
    uint8_t *q = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(q != MAP_FAILED);
    _Atomic uint32_t *a = (_Atomic uint32_t *)(s.base + 0x100), *b = (_Atomic uint32_t *)(q + 0x100);
    CHECK_U(atomic_load(b), 0);
    atomic_store(a, 0xfeed);
    CHECK_U(atomic_load(b), 0xfeed);
    atomic_fetch_add(b, 1);
    CHECK_U(atomic_load(a), 0xfeee);
    /* close-on-exec: children get it only by dup2 */
    CHECK((fcntl(s.fd, F_GETFD) & FD_CLOEXEC) != 0);
    munmap(q, 1 << 16);
    close(fd);
    /* across a fork */
    _Atomic uint32_t *w = (_Atomic uint32_t *)(s.base + 0x40);
    pid_t pid = fork();
    if (pid == 0) {
        atomic_store(w, 77);
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    CHECK_U(atomic_load(w), 77);
    char *path = mesh_shm_path(&s);
    int pfd = open(path, O_RDWR);
    CHECK(pfd >= 0);
    close(pfd);
    free(path);
    mesh_shm_close(&s);
    /* The full mailbox. */
    mesh_shm mb;
    CHECK(mesh_shm_new(&mb, &err));
    CHECK_U(mb.len, 0x800000);
    mesh_shm_close(&mb);
    return mesh_check_done();
}
