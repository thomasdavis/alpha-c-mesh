/* FIPS 180-4 / NIST vectors, and the same digest however the bytes are split. */
#include "mesh/check.h"
#include "sha256_stream.h"

static void hex_of(const char *msg, size_t n, size_t chunk, char out[65]) {
    sha256_stream s;
    sha256_stream_init(&s);
    for (size_t i = 0; i < n; i += chunk) sha256_stream_update(&s, msg + i, n - i < chunk ? n - i : chunk);
    uint8_t d[32];
    sha256_stream_final(&s, d);
    sha256_stream_hex(d, out);
}

int main(void) {
    char h[65];
    hex_of("", 0, 1, h);
    CHECK_S(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    hex_of("abc", 3, 1, h);
    CHECK_S(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (size_t c = 1; c <= 64; c++) {
        hex_of(two, strlen(two), c, h);
        CHECK_S(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }
    static char million[1000000];
    memset(million, 'a', sizeof million);
    hex_of(million, sizeof million, 4093, h);
    CHECK_S(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    return mesh_check_done();
}
