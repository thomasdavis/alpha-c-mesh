/* mesh_words_host: loads, stores, CAS and add see each other; byte copies are little-endian words. */
#include "mesh/words_host.h"
#include "mesh/check.h"

int main(void) {
    _Alignas(4) static uint8_t mem[64];
    mesh_words w = mesh_words_host(mem);
    mesh_words_store(&w, 8, 0x11223344u);
    CHECK_U(mesh_words_load(&w, 8), 0x11223344u);
    CHECK_U(mem[8], 0x44); /* little-endian on this host, as the mailbox is */
    CHECK_U(mesh_words_cas(&w, 8, 1, 2), 0x11223344u); /* fails: returns what it saw */
    CHECK_U(mesh_words_cas(&w, 8, 0x11223344u, 7), 0x11223344u);
    CHECK_U(mesh_words_add(&w, 8, 3), 7);
    CHECK_U(mesh_words_add(&w, 8, UINT32_MAX), 10); /* -1 */
    CHECK_U(mesh_words_load(&w, 8), 9);
    const uint8_t in[6] = {1, 2, 3, 4, 5, 6};
    uint8_t out[6] = {0};
    mesh_words_write(&w, 16, in, 6);
    CHECK_U(mesh_words_load(&w, 16), 0x04030201u);
    CHECK_U(mesh_words_load(&w, 20), 0x0605u); /* a partial last word: the rest is zero */
    mesh_words_read(&w, 16, out, 6);
    CHECK(memcmp(in, out, 6) == 0);
    mesh_words_fence(&w);
    return mesh_check_done();
}
