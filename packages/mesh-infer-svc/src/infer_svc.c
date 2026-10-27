/* The members' service addresses (infer.rs Svc). */
#include "mesh/infer_svc.h"
#include "mesh/buf.h"
#include "mesh/rsfmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "host:port" split at the last ':'; the port a u16. */
static int split_hp(const char *a, char *host, size_t hn, uint16_t *port, char **err) {
    const char *c = strrchr(a, ':');
    mesh_buf e = MESH_BUF_INIT;
    if (!c) {
        mesh_rs_debug_cstr(&e, a);
        mesh_buf_puts(&e, ": host:port");
        *err = mesh_buf_take(&e);
        return -1;
    }
    const char *p = c + 1 + (c[1] == '+' && c[2]);
    unsigned long v = 0;
    int ok = *p != 0;
    for (; ok && *p; p++) { ok = *p >= '0' && *p <= '9'; v = v * 10 + (unsigned long)(*p - '0'); ok = ok && v <= 65535; }
    if (!ok) {
        mesh_rs_debug_cstr(&e, a);
        mesh_buf_puts(&e, ": bad port");
        *err = mesh_buf_take(&e);
        return -1;
    }
    snprintf(host, hn, "%.*s", (int)(c - a), a);
    *port = (uint16_t)v;
    return 0;
}

int mesh_infer_svc_single(mesh_infer_svc *s, const char *control, char **err) {
    memset(s, 0, sizeof *s);
    char host[256];
    uint16_t port;
    if (split_hp(control, host, sizeof host, &port, err)) return -1;
    s->n = 1;
    strcpy(s->hosts[0], "127.0.0.1");
    s->bases[0] = port;
    return 0;
}

int mesh_infer_svc_cluster(mesh_infer_svc *s, const char *(*get)(void *, const char *), void *ctx, uint32_t me, uint32_t n,
                           char **err) {
    memset(s, 0, sizeof *s);
    s->me = me;
    for (uint32_t k = 0; k < n && k < MESH_INFER_SVC_MAX; k++) {
        char key[32];
        snprintf(key, sizeof key, "%u.peer", k);
        const char *a = get(ctx, key);
        if (!a) { *err = mesh_buf_fmt("cluster: no %u.peer", k); return -1; }
        if (split_hp(a, s->hosts[k], sizeof s->hosts[k], &s->bases[k], err)) return -1;
        snprintf(key, sizeof key, "%u.lan", k);
        const char *lan = get(ctx, key);
        snprintf(s->lans[k], sizeof s->lans[k], "%s", lan ? lan : "");
        s->n = k + 1;
    }
    return 0;
}

/* An IPv4 address's first three octets (std's strict parser: four decimal octets, no leading zeros). */
static int subnet24(const char *h, unsigned char o[3]) {
    unsigned v[4];
    const char *p = h;
    for (int i = 0; i < 4; i++) {
        const char *s = p;
        unsigned x = 0;
        while (*p >= '0' && *p <= '9' && p - s < 3) { x = x * 10 + (unsigned)(*p - '0'); p++; }
        if (p == s || x > 255 || (p - s > 1 && *s == '0')) return 0;
        v[i] = x;
        if (i < 3) { if (*p != '.') return 0; p++; }
    }
    if (*p) return 0;
    for (int i = 0; i < 3; i++) o[i] = (unsigned char)v[i];
    return 1;
}

void mesh_infer_svc_addr(const mesh_infer_svc *s, uint32_t from, uint32_t to, uint32_t param, char out[300]) {
    uint32_t port = (uint32_t)s->bases[to] + param;
    if (from == to) { snprintf(out, 300, "127.0.0.1:%u", port); return; }
    unsigned char a[3], b[3];
    int same = subnet24(s->hosts[from], a) && subnet24(s->hosts[to], b) && memcmp(a, b, 3) == 0;
    const char *host = same || !s->lans[to][0] ? s->hosts[to] : s->lans[to];
    snprintf(out, 300, "%s:%u", host, port);
}
