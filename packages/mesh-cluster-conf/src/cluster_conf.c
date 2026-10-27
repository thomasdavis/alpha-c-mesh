/* cluster.rs: keys and topology. */
#include "mesh/cluster_conf.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"
#include "mesh/mt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

mcc_tunables mcc_tunables_default(void) { return (mcc_tunables){MESH_PEER_PING_MS, MESH_PEER_DEAD_MS, msw_default()}; }

/* Rust's trim_matches('"'): every leading and trailing quote. */
static char *unquote(const char *s) {
    size_t n = strlen(s);
    while (n && s[0] == '"') s++, n--;
    while (n && s[n - 1] == '"') n--;
    return mt_strndup(s, n);
}

bool mcc_keys(const char *text, ms_map *out, char **err) {
    *out = (ms_map){0};
    mt_strs lines = mt_lines(text);
    bool ok = true;
    for (size_t i = 0; i < lines.n && ok; i++) {
        const char *raw = lines.v[i];
        const char *hash = strchr(raw, '#');
        char *pre = hash ? mt_strndup(raw, (size_t)(hash - raw)) : mt_strdup(raw);
        char *line = mt_trim(pre);
        free(pre);
        char *k, *v;
        if (!*line) {
        } else if (!mt_split_once(line, '=', &k, &v)) {
            char *q = rf_debug_str(raw);
            *err = mt_fmt("line %zu: expected key = value, got %s", i + 1, q);
            free(q);
            ok = false;
        } else {
            char *kt = mt_trim(k), *vt = mt_trim(v);
            char *vq = unquote(vt);
            if (!*kt) {
                *err = mt_fmt("line %zu: empty key", i + 1);
                ok = false;
            } else if (!ms_set(out, kt, vq, false)) {
                *err = mt_fmt("line %zu: %s given twice", i + 1, kt);
                ok = false;
            }
            free(k), free(v), free(kt), free(vt), free(vq);
        }
        free(line);
    }
    mt_strs_free(&lines);
    if (!ok) ms_free(out);
    return ok;
}

/* `k.peer` must be host:port (a non-empty host, a u16 port after the last ':'). */
static bool peer_addr(const ms_map *m, uint32_t k, char **out, char **err) {
    char key[32];
    snprintf(key, sizeof key, "%u.peer", k);
    const char *a = ms_get(m, key);
    if (!a) {
        *err = mt_fmt("cluster: no `%u.peer = host:port`", k);
        return false;
    }
    const char *colon = strrchr(a, ':');
    uint16_t port;
    if (!colon || colon == a || !mp_u16(colon + 1, &port)) {
        char *q = rf_debug_str(a);
        *err = mt_fmt("cluster: %u.peer = %s: expected host:port", k, q);
        free(q);
        return false;
    }
    *out = mt_strdup(a);
    return true;
}

static bool millis(const ms_map *m, const char *k, uint64_t d, uint64_t *out, char **err) {
    const char *v = ms_get(m, k);
    if (!v) {
        *out = d;
        return true;
    }
    if (mp_u64(v, out)) return true;
    char *q = rf_debug_str(v);
    *err = mt_fmt("cluster: %s = %s: milliseconds", k, q);
    free(q);
    return false;
}

bool mcc_topology_parse(const char *text, uint32_t me, mcc_topology *out, char **err) {
    ms_map m;
    if (!mcc_keys(text, &m, err)) return false;
    char *peers[MESH_MAX_SYSTEMS] = {0};
    uint32_t n = 0;
    bool ok = false;
    const char *sys = ms_get(&m, "systems");
    if (!sys) {
        *err = mt_strdup("cluster: no `systems = N`");
        goto done;
    }
    if (!mp_u32(sys, &n)) {
        *err = mt_strdup("cluster: systems: not a number");
        goto done;
    }
    if (n < 2 || n > MESH_MAX_SYSTEMS) {
        *err = mt_fmt("cluster: systems = %u: 2..=%u", n, (unsigned)MESH_MAX_SYSTEMS);
        goto done;
    }
    if (me >= n) {
        *err = mt_fmt("cluster: --system %u, but the cluster has systems 0..%u", me, n - 1);
        goto done;
    }
    for (uint32_t k = 0; k < n; k++)
        if (!peer_addr(&m, k, &peers[k], err)) goto done;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < i; j++)
            if (strcmp(peers[j], peers[i]) == 0) {
                *err = mt_fmt("cluster: systems %u and %u share the peer address %s", j, i, peers[i]);
                goto done;
            }
    mcc_topology t = {.me = me, .n = n};
    if (me > 0) {
        char key[32];
        snprintf(key, sizeof key, "%u.listen", me);
        const char *l = ms_get(&m, key);
        t.listen = mt_strdup(l ? l : peers[me]);
    }
    for (uint32_t j = me + 1; j < n; j++) {
        t.dial_k[t.ndial] = j;
        t.dial_addr[t.ndial++] = mt_strdup(peers[j]);
    }
    if (!millis(&m, "ping_ms", MESH_PEER_PING_MS, &t.tun.ping_ms, err) || !millis(&m, "dead_ms", MESH_PEER_DEAD_MS, &t.tun.dead_ms, err) ||
        !msw_from_keys(&m, &t.tun.sched, err)) {
        mcc_topology_free(&t);
        goto done;
    }
    if (t.tun.ping_ms == 0 || t.tun.dead_ms < 3 * t.tun.ping_ms) {
        *err = mt_fmt("cluster: dead_ms %llu must be at least 3 x ping_ms %llu", (unsigned long long)t.tun.dead_ms, (unsigned long long)t.tun.ping_ms);
        mcc_topology_free(&t);
        goto done;
    }
    *out = t;
    ok = true;
done:
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) free(peers[k]);
    ms_free(&m);
    return ok;
}

mcc_topology mcc_pair_dialer(const char *peer) {
    mcc_topology t = {.me = 0, .n = 2, .ndial = 1, .tun = mcc_tunables_default()};
    t.dial_k[0] = 1;
    t.dial_addr[0] = mt_strdup(peer);
    return t;
}

mcc_topology mcc_pair_listener(const char *addr) {
    return (mcc_topology){.me = 1, .n = 2, .listen = mt_strdup(addr), .tun = mcc_tunables_default()};
}

bool mcc_topology_eq(const mcc_topology *a, const mcc_topology *b) {
    if (a->me != b->me || a->n != b->n || !mt_eq(a->listen, b->listen) || a->ndial != b->ndial) return false;
    for (size_t i = 0; i < a->ndial; i++)
        if (a->dial_k[i] != b->dial_k[i] || strcmp(a->dial_addr[i], b->dial_addr[i]) != 0) return false;
    return a->tun.ping_ms == b->tun.ping_ms && a->tun.dead_ms == b->tun.dead_ms && msw_eq(&a->tun.sched, &b->tun.sched);
}

void mcc_topology_free(mcc_topology *t) {
    free(t->listen);
    for (size_t i = 0; i < t->ndial; i++) free(t->dial_addr[i]);
    *t = (mcc_topology){0};
}
