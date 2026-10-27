/* One-shot replies over mesh_chan. */
#include "mesh/reply.h"
#include "mesh/buf.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void drop_result(void *p) { mesh_result_free(p); }

mesh_reply *mesh_reply_new(void) { return mesh_chan_new(sizeof(mesh_result), 0, drop_result); }

mesh_reply *mesh_reply_sender(mesh_reply *r) {
    mesh_chan_retain(r);
    return r;
}

mesh_reply *mesh_reply_clone(mesh_reply *r) {
    mesh_chan_retain(r);
    mesh_chan_add_sender(r);
    return r;
}

void mesh_reply_send(mesh_reply *r, mesh_result res) {
    if (!r) { mesh_result_free(&res); return; }
    if (mesh_chan_send(r, &res) != MESH_CHAN_OK) mesh_result_free(&res);
    mesh_chan_drop_sender(r);
    mesh_chan_release(r);
}

void mesh_reply_drop(mesh_reply *r) {
    if (!r) return;
    mesh_chan_drop_sender(r);
    mesh_chan_release(r);
}

mesh_result mesh_result_ok(const char *text) {
    mesh_result x = {true, 0, mesh_xstrdup(text ? text : ""), NULL, 0, NULL, NULL};
    return x;
}

mesh_result mesh_result_err(const char *text) {
    mesh_result x = {false, 0, mesh_xstrdup(text ? text : ""), NULL, 0, NULL, NULL};
    return x;
}

mesh_result mesh_result_copy(const mesh_result *r) {
    mesh_result x = *r;
    x.ptr = NULL;
    x.ptr_free = NULL;
    if (r->text) x.text = mesh_xstrdup(r->text);
    if (r->bytes) {
        x.bytes = mesh_xmalloc(r->len);
        memcpy(x.bytes, r->bytes, r->len);
    }
    return x;
}

void mesh_reply_ok(mesh_reply *r, const char *text) { mesh_reply_send(r, mesh_result_ok(text)); }
void mesh_reply_err(mesh_reply *r, const char *text) { mesh_reply_send(r, mesh_result_err(text)); }

void mesh_reply_okf(mesh_reply *r, const char *fmt, ...) {
    mesh_buf b = MESH_BUF_INIT;
    va_list ap;
    va_start(ap, fmt);
    mesh_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    mesh_result x = {true, 0, mesh_buf_take(&b), NULL, 0, NULL, NULL};
    mesh_reply_send(r, x);
}

void mesh_reply_errf(mesh_reply *r, const char *fmt, ...) {
    mesh_buf b = MESH_BUF_INIT;
    va_list ap;
    va_start(ap, fmt);
    mesh_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    mesh_result x = {false, 0, mesh_buf_take(&b), NULL, 0, NULL, NULL};
    mesh_reply_send(r, x);
}

void mesh_reply_value(mesh_reply *r, uint32_t v) {
    mesh_result x = {true, v, NULL, NULL, 0, NULL, NULL};
    mesh_reply_send(r, x);
}

void mesh_reply_bytes(mesh_reply *r, const void *p, size_t n) {
    mesh_result x = {true, 0, NULL, mesh_xmalloc(n), n, NULL, NULL};
    if (n) memcpy(x.bytes, p, n);
    mesh_reply_send(r, x);
}

void mesh_reply_ptr(mesh_reply *r, void *ptr, void (*ptr_free)(void *ptr)) {
    mesh_result x = {true, 0, NULL, NULL, 0, ptr, ptr_free};
    mesh_reply_send(r, x);
}

int mesh_reply_wait(mesh_reply *r, int64_t timeout_ms, mesh_result *out) {
    memset(out, 0, sizeof *out);
    return mesh_chan_recv(r, out, timeout_ms);
}

int mesh_reply_poll(mesh_reply *r, mesh_result *out) {
    memset(out, 0, sizeof *out);
    return mesh_chan_try_recv(r, out);
}

void mesh_reply_release(mesh_reply *r) {
    if (!r) return;
    mesh_chan_close_rx(r);
    mesh_chan_release(r);
}

void mesh_result_free(mesh_result *r) {
    free(r->text);
    free(r->bytes);
    if (r->ptr && r->ptr_free) r->ptr_free(r->ptr);
    r->ptr = NULL;
    r->text = NULL;
    r->bytes = NULL;
    r->len = 0;
}
