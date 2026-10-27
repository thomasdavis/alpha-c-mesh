/* The word access and the byte copies. */
#include "mesh/check.h"
#include "mesh/mailbox.h"

int main(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_mailbox_set(&mb, 8, 7);
    CHECK_U(mesh_mailbox_get(&mb, 8), 7);
    CHECK_U(mesh_mailbox_fetch_add(&mb, 8, 2), 7);
    CHECK_U(mesh_mailbox_cas(&mb, 8, 9, 1), 9);
    CHECK_U(mesh_mailbox_cas(&mb, 8, 5, 2), 1);
    mesh_mailbox_set64(&mb, 16, 0x0102030405060708ull);
    CHECK_U(mesh_mailbox_get(&mb, 16), 0x05060708u);
    mesh_mailbox_write(&mb, 32, "hello", 5, 8);
    uint8_t b[8];
    mesh_mailbox_read(&mb, 32, b, 8);
    CHECK(b[0] == 'h' && b[4] == 'o' && b[5] == 0 && b[7] == 0);
    free(mb.base);
    return mesh_check_done();
}
