/* Frames over a socket, messages, layers. */
#include "mesh/infer_wire.h"
#include "mesh/chan.h"
#include "mesh/rsfmt.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_all(int fd, const void *p, size_t n) {
    const char *c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        c += w;
        n -= (size_t)w;
    }
    return 0;
}

int mesh_infer_write_frame(int fd, uint8_t kind, const void *body, size_t len) {
    uint8_t head[5];
    uint32_t n = (uint32_t)len + 1;
    for (int i = 0; i < 4; i++) head[i] = (uint8_t)(n >> (8 * i));
    head[4] = kind;
    if (len < 4096) {
        mesh_buf b = MESH_BUF_INIT;
        mesh_buf_put(&b, head, 5);
        mesh_buf_put(&b, body, len);
        int r = write_all(fd, b.data, b.len);
        mesh_buf_free(&b);
        return r;
    }
    if (write_all(fd, head, 5) != 0) return -1;
    return write_all(fd, body, len);
}

/* std's io::Error text for a failed read_exact. */
static char *io_err(int e, int eof) {
    if (eof) return mesh_xstrdup("read: failed to fill whole buffer");
    return mesh_buf_fmt("read: %s (os error %d)", strerror(e), e);
}

static int read_exact(int fd, void *p, size_t n, char **err) {
    char *c = p;
    while (n) {
        ssize_t r = read(fd, c, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) { *err = io_err(errno, r == 0); return -1; }
        c += r;
        n -= (size_t)r;
    }
    return 0;
}

int mesh_infer_read_frame(int fd, uint8_t *kind, mesh_buf *body, int64_t *wire_us, char **err) {
    uint8_t head[5];
    if (read_exact(fd, head, 5, err) != 0) return -1;
    int64_t t0 = mesh_mono_us();
    uint32_t n = (uint32_t)head[0] | (uint32_t)head[1] << 8 | (uint32_t)head[2] << 16 | (uint32_t)head[3] << 24;
    if (n == 0 || n - 1 > MESH_INFER_MAX_FRAME) { *err = mesh_buf_fmt("read: a frame of %u bytes", n); return -1; }
    size_t at = body->len;
    mesh_buf_zeros(body, n - 1);
    if (read_exact(fd, body->data + at, n - 1, err) != 0) { mesh_buf_truncate(body, at); return -1; }
    *kind = head[4];
    if (wire_us) *wire_us = mesh_mono_us() - t0;
    return 0;
}

void mesh_infer_msg_init(mesh_infer_msg *m) {
    memset(m, 0, sizeof *m);
    m->body = mesh_xstrdup("");
}

void mesh_infer_msg_free(mesh_infer_msg *m) {
    for (size_t i = 0; i < m->n; i++) { free(m->k[i]); free(m->v[i]); }
    free(m->k);
    free(m->v);
    free(m->body);
    memset(m, 0, sizeof *m);
}

void mesh_infer_msg_set(mesh_infer_msg *m, const char *k, const char *v) {
    size_t i = 0;
    while (i < m->n && strcmp(m->k[i], k) < 0) i++;
    if (i < m->n && strcmp(m->k[i], k) == 0) {
        free(m->v[i]);
        m->v[i] = mesh_xstrdup(v);
        return;
    }
    if (m->n == m->cap) {
        m->cap = m->cap ? 2 * m->cap : 8;
        m->k = mesh_xrealloc(m->k, m->cap * sizeof *m->k);
        m->v = mesh_xrealloc(m->v, m->cap * sizeof *m->v);
    }
    memmove(m->k + i + 1, m->k + i, (m->n - i) * sizeof *m->k);
    memmove(m->v + i + 1, m->v + i, (m->n - i) * sizeof *m->v);
    m->k[i] = mesh_xstrdup(k);
    m->v[i] = mesh_xstrdup(v);
    m->n++;
}

void mesh_infer_msg_setf(mesh_infer_msg *m, const char *k, const char *fmt, ...) {
    mesh_buf b = MESH_BUF_INIT;
    va_list ap;
    va_start(ap, fmt);
    mesh_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    mesh_infer_msg_set(m, k, mesh_buf_str(&b));
    mesh_buf_free(&b);
}

void mesh_infer_msg_set_body(mesh_infer_msg *m, const char *body) {
    free(m->body);
    m->body = mesh_xstrdup(body);
}

const char *mesh_infer_msg_get(const mesh_infer_msg *m, const char *k) {
    for (size_t i = 0; i < m->n; i++) if (strcmp(m->k[i], k) == 0) return m->v[i];
    return NULL;
}

int mesh_infer_msg_num(const mesh_infer_msg *m, const char *k, uint64_t *out, char **err) {
    const char *v = mesh_infer_msg_get(m, k);
    if (!v) { *err = mesh_buf_fmt("missing %s", k); return -1; }
    const char *p = v + (*v == '+');
    uint64_t x = 0;
    int ok = *p != 0;
    for (; ok && *p; p++) {
        ok = *p >= '0' && *p <= '9' && x <= (UINT64_MAX - (uint64_t)(*p - '0')) / 10;
        x = x * 10 + (uint64_t)(*p - '0');
    }
    if (!ok) { *err = mesh_buf_fmt("%s: not a number", k); return -1; }
    *out = x;
    return 0;
}

void mesh_infer_msg_encode(const mesh_infer_msg *m, mesh_buf *out) {
    for (size_t i = 0; i < m->n; i++) {
        mesh_buf_printf(out, "%s=", m->k[i]);
        for (const char *c = m->v[i]; *c; c++) mesh_buf_putc(out, *c == '\n' ? ' ' : *c);
        mesh_buf_putc(out, '\n');
    }
    mesh_buf_putc(out, '\n');
    mesh_buf_puts(out, m->body);
}

int mesh_infer_msg_decode(const void *bytes, size_t n, mesh_infer_msg *out, char **err) {
    mesh_infer_msg_init(out);
    if (!mesh_rs_utf8_valid(bytes, n) || memchr(bytes, 0, n)) {
        /* A NUL is valid UTF-8 in Rust; C strings cannot hold it: refused the same way. */
        *err = mesh_xstrdup("a message that is not UTF-8");
        mesh_infer_msg_free(out);
        return -1;
    }
    char *s = mesh_xstrndup(bytes, n);
    char *sep = strstr(s, "\n\n");
    const char *body = "";
    if (sep) { *sep = 0; body = sep + 2; }
    else if (n && s[n - 1] == '\n') s[n - 1] = 0;
    char *line = s;
    int rc = 0;
    while (line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        size_t ln = strlen(line);
        if (ln && line[ln - 1] == '\r') line[--ln] = 0;
        if (ln) {
            char *eq = strchr(line, '=');
            if (!eq) {
                mesh_buf e = MESH_BUF_INIT;
                mesh_buf_puts(&e, "a header line without '=': ");
                mesh_rs_debug_cstr(&e, line);
                *err = mesh_buf_take(&e);
                rc = -1;
                break;
            }
            *eq = 0;
            mesh_infer_msg_set(out, line, eq + 1);
        }
        line = nl ? nl + 1 : NULL;
    }
    if (rc == 0) mesh_infer_msg_set_body(out, body);
    else mesh_infer_msg_free(out);
    free(s);
    return rc;
}

uint64_t mesh_infer_layer_sum(const void *bytes, size_t n) {
    const uint8_t *b = bytes;
    uint64_t s = 0;
    for (size_t i = 0; i < n; i += 8) {
        uint64_t w = 0;
        for (size_t k = 0; k < 8 && i + k < n; k++) w |= (uint64_t)b[i + k] << (8 * k);
        s += w;
    }
    return s;
}

void mesh_infer_layer_frame(mesh_buf *out, uint32_t layer, uint32_t rows, const void *bytes, size_t n) {
    mesh_buf_reserve(out, 16 + n);
    mesh_buf_u32le(out, layer);
    mesh_buf_u32le(out, rows);
    mesh_buf_u64le(out, mesh_infer_layer_sum(bytes, n));
    mesh_buf_put(out, bytes, n);
}

int mesh_infer_parse_layer(const void *bytes, size_t n, uint32_t *layer, uint32_t *rows, const uint8_t **data, size_t *len,
                           char **err) {
    const uint8_t *b = bytes;
    if (n < 16) { *err = mesh_xstrdup("a short KV layer frame"); return -1; }
    uint32_t l = 0, r = 0;
    uint64_t sum = 0;
    for (int i = 0; i < 4; i++) { l |= (uint32_t)b[i] << (8 * i); r |= (uint32_t)b[4 + i] << (8 * i); }
    for (int i = 0; i < 8; i++) sum |= (uint64_t)b[8 + i] << (8 * i);
    if (mesh_infer_layer_sum(b + 16, n - 16) != sum) { *err = mesh_buf_fmt("KV layer %u: checksum mismatch (%zu bytes)", l, n - 16); return -1; }
    *layer = l;
    *rows = r;
    *data = b + 16;
    *len = n - 16;
    return 0;
}
