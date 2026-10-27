/* Appending, printf, little-endian words, take and truncate. */
#include "mesh/buf.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    mesh_buf b = MESH_BUF_INIT;
    CHECK_S(mesh_buf_str(&b), "");
    mesh_buf_puts(&b, "ok");
    mesh_buf_putc(&b, ' ');
    mesh_buf_printf(&b, "%u us %s", 42u, "done");
    CHECK_S(b.data, "ok 42 us done");
    CHECK_U(b.len, 13);
    for (int i = 0; i < 1000; i++) mesh_buf_putc(&b, 'x');
    CHECK_U(b.len, 1013);
    CHECK_U(b.data[1013], 0);
    mesh_buf_truncate(&b, 2);
    CHECK_S(b.data, "ok");
    mesh_buf_clear(&b);
    mesh_buf_u32le(&b, 0x04030201u);
    mesh_buf_u64le(&b, 0x0c0b0a0908070605ull);
    mesh_buf_zeros(&b, 2);
    const unsigned char want[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0};
    CHECK_U(b.len, sizeof want);
    CHECK(memcmp(b.data, want, sizeof want) == 0);
    char *s = mesh_buf_take(&b);
    CHECK(b.data == NULL && b.len == 0);
    free(s);
    char *f = mesh_buf_fmt("%s=%d", "a", 7);
    CHECK_S(f, "a=7");
    free(f);
    char *d = mesh_xstrndup("hello", 3);
    CHECK_S(d, "hel");
    free(d);
    mesh_buf_free(&b);
    return mesh_check_done();
}
