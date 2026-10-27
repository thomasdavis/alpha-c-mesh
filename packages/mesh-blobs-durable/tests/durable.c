/* Writes are whole under their name (no .tmp left), links share the inode, a link whose source went is
 * written instead, and directories sync. */
#include "mesh/blobs/durable.h"
#include "mesh/check.h"
#include "mesh/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *slurp(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    char *b = mxalloc(4096);
    *n = fread(b, 1, 4096, f);
    fclose(f);
    return b;
}

int main(void) {
    char *root = mfmt("%s/mesh-blobs-durable-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    mb_remove_tree(root);
    char *a = mfmt("%s/1.1", root), *b = mfmt("%s/2.1", root);
    char *err = NULL;
    CHECK(mb_make_durable(a, "h1", NULL, "pass sums", 9, &err));
    CHECK(mb_sync_dir(a, &err));
    char *pa = mfmt("%s/h1", a), *ta = mfmt("%s/h1.tmp", a);
    size_t n;
    char *got = slurp(pa, &n);
    CHECK(got && n == 9 && memcmp(got, "pass sums", 9) == 0);
    free(got);
    struct stat st, st2;
    CHECK(stat(ta, &st) != 0);
    /* Another job's copy: a hard link, the same inode. */
    CHECK(mb_make_durable(b, "h1", pa, "pass sums", 9, &err));
    char *pb = mfmt("%s/h1", b);
    CHECK(stat(pa, &st) == 0 && stat(pb, &st2) == 0 && st.st_ino == st2.st_ino);
    /* Linking again over an existing name replaces it. */
    CHECK(mb_make_durable(b, "h1", pa, "pass sums", 9, &err));
    /* A link whose source went: the bytes are written. */
    char *c = mfmt("%s/3.1", root);
    CHECK(mb_make_durable(c, "h2", "/nonexistent/file", "late", 4, &err));
    char *pc = mfmt("%s/h2", c);
    got = slurp(pc, &n);
    CHECK(got && n == 4 && memcmp(got, "late", 4) == 0);
    free(got);
    CHECK(!mb_sync_dir("/nonexistent/dir", &err));
    CHECK(err && strstr(err, "/nonexistent/dir") != NULL);
    free(err);
    mb_remove_tree(root);
    CHECK(stat(root, &st) != 0);
    for (char *p[] = {root, a, b, pa, ta, pb, c, pc}, **q = p; q < p + 8; q++) free(*q);
    return mesh_check_done();
}
