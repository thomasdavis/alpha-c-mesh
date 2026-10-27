/* mesh-strbuf: appends, printf growth, take, and formatted strings. */
#include "mesh/strbuf.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    msb b = {0};
    CHECK_S(msb_str(&b), "");
    msb_puts(&b, "jobs");
    msb_putc(&b, ' ');
    msb_printf(&b, "system %u leader %s", 3u, "-");
    CHECK_S(msb_str(&b), "jobs system 3 leader -");
    for (int i = 0; i < 1000; i++) msb_printf(&b, "%d,", i);
    CHECK(b.n > 3000 && b.p[b.n] == 0);
    msb_clear(&b);
    CHECK_S(msb_str(&b), "");
    msb_put(&b, "a\0b", 3);
    CHECK_U(b.n, 3);
    char *s = msb_take(&b);
    CHECK(memcmp(s, "a\0b", 4) == 0 && b.p == NULL && b.n == 0);
    free(s);
    char *f = mfmt("%s %05.1f %llx", "x", 2.25, 0xabcULL);
    CHECK_S(f, "x 002.2 abc");
    free(f);
    char *d = mstrndup("hello", 3);
    CHECK_S(d, "hel");
    free(d);
    msb_free(&b);
    return mesh_check_done();
}
