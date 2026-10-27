/* infer.rs's request parsing, word by word as the Rust does it. */
#include "mesh/infer_request.h"
#include "mesh/rsfmt.h"

#include <stdlib.h>
#include <string.h>

/* char::is_whitespace for ASCII and the common Unicode spaces (the prompt's split points). */
static size_t ws_len(const char *s) {
    unsigned char c = (unsigned char)s[0];
    if (c == ' ' || (c >= 9 && c <= 13)) return 1;
    if (c == 0xc2 && ((unsigned char)s[1] == 0x85 || (unsigned char)s[1] == 0xa0)) return 2;
    if (c == 0xe1 && (unsigned char)s[1] == 0x9a && (unsigned char)s[2] == 0x80) return 3;
    if (c == 0xe2 && (unsigned char)s[1] == 0x80 && (((unsigned char)s[2] >= 0x80 && (unsigned char)s[2] <= 0x8a) ||
                                                     (unsigned char)s[2] == 0xa8 || (unsigned char)s[2] == 0xa9 || (unsigned char)s[2] == 0xaf))
        return 3;
    if (c == 0xe2 && (unsigned char)s[1] == 0x81 && (unsigned char)s[2] == 0x9f) return 3;
    if (c == 0xe3 && (unsigned char)s[1] == 0x80 && (unsigned char)s[2] == 0x80) return 3;
    return 0;
}

static const char *trim_start(const char *s) {
    size_t k;
    while (*s && (k = ws_len(s))) s += k;
    return s;
}

/* split_once(char::is_whitespace): the word's end and what follows the separator. */
static void split_ws(const char *s, size_t *wlen, const char **after) {
    const char *p = s;
    size_t k = 0;
    while (*p && !(k = ws_len(p))) p++;
    *wlen = (size_t)(p - s);
    *after = *p ? p + k : p;
}

/* u32::from_str: optional '+', digits, no overflow. */
static int u32_of(const char *s, size_t n, uint32_t *out) {
    size_t i = n > 0 && s[0] == '+' && n > 1;
    if (i == n) return -1;
    uint64_t x = 0;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        x = x * 10 + (uint64_t)(s[i] - '0');
        if (x > UINT32_MAX) return -1;
    }
    *out = (uint32_t)x;
    return 0;
}

static int num(const char *w, size_t wn, const char *v, size_t vn, uint32_t *out, char **err) {
    if (u32_of(v, vn, out) == 0) return 0;
    *err = mesh_buf_fmt("%.*s: not a number (%s)", (int)wn, w, MESH_INFER_USAGE);
    return -1;
}

static int prefixed(const char *w, size_t n, const char *p) { size_t k = strlen(p); return n >= k && strncmp(w, p, k) == 0; }

int mesh_infer_parse(const char *arg, mesh_infer_request *r, char **err) {
    memset(r, 0, sizeof *r);
    strcpy(r->mode, "fast");
    r->max = 32;
    const char *rest = trim_start(arg);
    for (;;) {
        size_t wn;
        const char *after;
        split_ws(rest, &wn, &after);
        const char *w = rest;
        if ((wn == 4 && !strncmp(w, "fast", 4)) || (wn == 6 && !strncmp(w, "fast16", 6)) || (wn == 5 && !strncmp(w, "exact", 5)) ||
            (wn == 9 && !strncmp(w, "exact-cpu", 9))) {
            memcpy(r->mode, w, wn);
            r->mode[wn] = 0;
        } else if (wn == 3 && !strncmp(w, "max", 3)) {
            const char *a = trim_start(after), *a2;
            size_t vn;
            split_ws(a, &vn, &a2);
            if (num("max", 3, a, vn, &r->max, err)) return -1;
            rest = trim_start(a2);
            continue;
        } else if (prefixed(w, wn, "max=")) {
            if (num(w, wn, w + 4, wn - 4, &r->max, err)) return -1;
        } else if (prefixed(w, wn, "prefill=")) {
            if (num(w, wn, w + 8, wn - 8, &r->prefill, err)) return -1;
            r->has_prefill = true;
        } else if (prefixed(w, wn, "decode=")) {
            if (num(w, wn, w + 7, wn - 7, &r->decode, err)) return -1;
            r->has_decode = true;
        } else if (prefixed(w, wn, "mono=")) {
            if (num(w, wn, w + 5, wn - 5, &r->mono, err)) return -1;
            r->has_mono = true;
        } else if (prefixed(w, wn, "len=")) {
            if (num(w, wn, w + 4, wn - 4, &r->len, err)) return -1;
            r->has_len = true;
        } else if (prefixed(w, wn, "repeat=")) {
            if (num(w, wn, w + 7, wn - 7, &r->repeat, err)) return -1;
            if (r->repeat < 1) r->repeat = 1;
            if (r->repeat > 1000) r->repeat = 1000;
            r->has_repeat = true;
        } else break;
        rest = trim_start(after);
    }
    /* trim_end */
    size_t n = strlen(rest);
    while (n) {
        size_t back = 0;
        for (size_t k = 1; k <= 3 && k <= n; k++) if (ws_len(rest + n - k) == k) { back = k; break; }
        if (!back) break;
        n -= back;
    }
    r->prompt = mesh_xstrndup(rest, n);
    if (!n) { *err = mesh_buf_fmt("no prompt (%s)", MESH_INFER_USAGE); return -1; }
    if (r->max == 0 || r->max > 1000) { *err = mesh_buf_fmt("max %u: 1..=1000", r->max); return -1; }
    if (r->has_mono && (r->has_prefill || r->has_decode)) { *err = mesh_buf_fmt("mono= or prefill=/decode=, not both (%s)", MESH_INFER_USAGE); return -1; }
    if (r->has_prefill != r->has_decode) { *err = mesh_buf_fmt("prefill= and decode= go together (%s)", MESH_INFER_USAGE); return -1; }
    return 0;
}

void mesh_infer_request_free(mesh_infer_request *r) {
    free(r->prompt);
    r->prompt = NULL;
}

void mesh_infer_text(mesh_buf *out, const mesh_infer_msg *res) {
    for (size_t i = 0; i < res->n; i++) mesh_buf_printf(out, "%s %s\n", res->k[i], res->v[i]);
    mesh_buf_printf(out, "text %s\n", res->body);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char *mesh_infer_url_decode(const char *s) {
    mesh_buf raw = MESH_BUF_INIT;
    size_t n = strlen(s);
    for (size_t i = 0; i < n;) {
        if (s[i] == '+') { mesh_buf_putc(&raw, ' '); i++; continue; }
        /* u8::from_str_radix of the two bytes: a sign or anything not hex is no escape. */
        if (s[i] == '%' && i + 3 <= n) {
            int a = hexval(s[i + 1]), b = hexval(s[i + 2]);
            if (a >= 0 && b >= 0) { mesh_buf_putc(&raw, (char)(a * 16 + b)); i += 3; continue; }
            if (s[i + 1] == '+' && b >= 0) { mesh_buf_putc(&raw, (char)b); i += 3; continue; }
        }
        mesh_buf_putc(&raw, s[i]);
        i++;
    }
    mesh_buf out = MESH_BUF_INIT;
    mesh_rs_lossy(&out, raw.data ? raw.data : "", raw.len);
    mesh_buf_free(&raw);
    return mesh_buf_take(&out);
}

int mesh_infer_from_query(const char *q, mesh_infer_request *r, bool *full, char **err) {
    mesh_buf words = MESH_BUF_INIT;
    char *prompt = NULL;
    *full = false;
    int nw = 0, rc = 0;
    const char *p = q;
    while (*p && rc == 0) {
        const char *amp = strchr(p, '&');
        size_t n = amp ? (size_t)(amp - p) : strlen(p);
        if (n) {
            char *kv = mesh_xstrndup(p, n);
            char *eq = strchr(kv, '=');
            const char *k = kv, *vraw = "";
            if (eq) { *eq = 0; vraw = eq + 1; }
            char *v = mesh_infer_url_decode(vraw);
            if (!strcmp(k, "prompt")) { free(prompt); prompt = mesh_xstrdup(v); }
            else if (!strcmp(k, "mode")) { mesh_buf_printf(&words, "%s%s", nw++ ? " " : "", v); }
            else if (!strcmp(k, "max") || !strcmp(k, "prefill") || !strcmp(k, "decode") || !strcmp(k, "mono") || !strcmp(k, "len") ||
                     !strcmp(k, "repeat")) {
                mesh_buf_printf(&words, "%s%s=%s", nw++ ? " " : "", k, v);
            } else if (!strcmp(k, "format")) *full = !strcmp(v, "full");
            else {
                mesh_buf e = MESH_BUF_INIT;
                mesh_buf_puts(&e, "unknown parameter ");
                mesh_rs_debug_cstr(&e, k);
                mesh_buf_puts(&e, " (prompt, max, mode, prefill, decode, mono, format)");
                *err = mesh_buf_take(&e);
                rc = -1;
            }
            free(v);
            free(kv);
        }
        if (!amp) break;
        p = amp + 1;
    }
    if (rc == 0 && !prompt) { *err = mesh_xstrdup("no prompt= parameter"); rc = -1; }
    if (rc == 0) {
        char *line = mesh_buf_fmt("%s %s", mesh_buf_str(&words), prompt);
        rc = mesh_infer_parse(line, r, err);
        free(line);
    } else memset(r, 0, sizeof *r);
    free(prompt);
    mesh_buf_free(&words);
    return rc;
}
