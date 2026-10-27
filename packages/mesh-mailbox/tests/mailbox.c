/* Words, bytes, windows and the pipe words over a test mailbox. */
#include "mesh/check.h"
#include "mesh/mailbox.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    mesh_mailbox m = mesh_mailbox_test();
    mesh_mailbox_set(m, 0x100, 5);
    CHECK_U(mesh_mailbox_get(m, 0x100), 5);
    CHECK_U(mesh_mailbox_fetch_add(m, 0x100, 2), 5);
    CHECK_U(mesh_mailbox_cas(m, 0x100, 7, 9), 7);
    CHECK_U(mesh_mailbox_cas(m, 0x100, 7, 11), 9);
    mesh_mailbox_publish64(m, 0x108, UINT64_C(0x1122334455667788));
    CHECK_U(mesh_mailbox_get64(m, 0x108), UINT64_C(0x1122334455667788));
    CHECK_U(mesh_mailbox_fetch_add64(m, 0x108, 1), UINT64_C(0x1122334455667788));
    CHECK_U(mesh_mailbox_get8(m, 0x108), 0x89);
    mesh_mailbox_set8(m, 0x10b, 0xab);
    CHECK_U(mesh_mailbox_get(m, 0x108), 0xab667789);
    const uint8_t hello[] = "hello";
    mesh_mailbox_write(m, 0x200, hello, 5, 12);
    uint8_t out[12];
    mesh_mailbox_read(m, 0x200, out, 12);
    CHECK(memcmp(out, "hello\0\0\0\0\0\0\0", 12) == 0);
    mesh_mailbox_read(m, 0x200, out, 3);
    CHECK(memcmp(out, "hel", 3) == 0);
    mesh_mailbox_zero(m, 0x200, 8);
    CHECK_U(mesh_mailbox_get(m, 0x200), 0);
    mesh_words w = mesh_mailbox_words(m);
    CHECK_U(mesh_words_load(&w, 0x100), 9);
    mesh_words_store(&w, 0x104, 3);
    CHECK_U(mesh_mailbox_get(m, 0x104), 3);
    free(m.base);
    return mesh_check_done();
}
