/* Interface counters: the OS readers, the wrap-safe extension, the total, the records. */
#include "mesh/nic.h"
#include "mesh/buf.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

const size_t MESH_NIC_COUNTERS[8] = {MESH_NI_RX_BYTES, MESH_NI_TX_BYTES, MESH_NI_RX_PACKETS, MESH_NI_TX_PACKETS,
                                     MESH_NI_RX_ERRS,  MESH_NI_TX_ERRS,  MESH_NI_RX_DROP,    MESH_NI_TX_DROP};

mesh_nic_ext mesh_nic_ext_new(uint64_t raw) {
    mesh_nic_ext e = {raw, raw};
    return e;
}

uint64_t mesh_nic_ext_update(mesh_nic_ext *e, uint64_t raw, uint32_t bits) {
    uint64_t d;
    if (bits >= 64) d = raw >= e->last ? raw - e->last : raw;
    else {
        uint64_t m = (1ull << bits) - 1;
        d = ((raw & m) - (e->last & m)) & m;
    }
    e->last = raw;
    e->value += d;
    return d;
}

static void trim(const char **s, const char **e) {
    while (*s < *e && (**s == ' ' || **s == '\t')) (*s)++;
    while (*e > *s && ((*e)[-1] == ' ' || (*e)[-1] == '\t' || (*e)[-1] == '\r')) (*e)--;
}

int mesh_nic_parse_proc_net_dev(const char *text, mesh_nic_raw **out, size_t *n, char **err) {
    *out = NULL;
    *n = 0;
    size_t cap = 0, lineno = 0;
    const char *l = text;
    while (*l) {
        const char *e = strchr(l, '\n');
        const char *end = e ? e : l + strlen(l);
        lineno++;
        if (lineno > 2) {
            const char *colon = memchr(l, ':', (size_t)(end - l));
            if (!colon) { *err = mesh_fmt("/proc/net/dev line %zu: no ':'", lineno); free(*out); *out = NULL; *n = 0; return -1; }
            const char *ns = l, *ne = colon;
            trim(&ns, &ne);
            uint64_t f[32];
            size_t k = 0;
            const char *p = colon + 1;
            while (p < end) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
                if (p >= end) break;
                const char *w = p;
                while (p < end && !(*p == ' ' || *p == '\t' || *p == '\r')) p++;
                /* u64::from_str: digits only (a leading '+' allowed), else ParseIntError's words. */
                size_t wl = (size_t)(p - w), i0 = w[0] == '+' && wl > 1;
                int ok = 1, big = 0;
                uint64_t x = 0;
                for (size_t i = i0; ok && i < wl; i++) {
                    ok = w[i] >= '0' && w[i] <= '9';
                    if (ok && !big) {
                        unsigned d = (unsigned)(w[i] - '0');
                        if (x > (UINT64_MAX - d) / 10) big = 1;
                        else x = x * 10 + d;
                    }
                }
                if (ok && !big) f[k < 32 ? k : 31] = x;
                if (!ok || big) {
                    *err = mesh_fmt("/proc/net/dev line %zu: %s", lineno, ok ? "number too large to fit in target type" : "invalid digit found in string");
                    free(*out); *out = NULL; *n = 0;
                    return -1;
                }
                k++;
            }
            if (k < 16) { *err = mesh_fmt("/proc/net/dev line %zu: %zu fields, want 16", lineno, k); free(*out); *out = NULL; *n = 0; return -1; }
            if (*n == cap) { cap = cap ? 2 * cap : 16; *out = mesh_xrealloc(*out, cap * sizeof **out); }
            mesh_nic_raw *r = &(*out)[(*n)++];
            memset(r, 0, sizeof *r);
            size_t nl = (size_t)(ne - ns) < MESH_NIC_NAME_MAX - 1 ? (size_t)(ne - ns) : MESH_NIC_NAME_MAX - 1;
            memcpy(r->name, ns, nl);
            const uint64_t v[8] = {f[0], f[8], f[1], f[9], f[2], f[10], f[3], f[11]};
            for (int i = 0; i < 8; i++) { r->v[i] = v[i]; r->bits[i] = 64; }
        }
        if (!e) break;
        l = e + 1;
    }
    if (*n == 0) { *err = mesh_xstrdup("/proc/net/dev lists no interface"); return -1; }
    return 0;
}

#if defined(__linux__)
int mesh_nic_read(mesh_nic_raw **out, size_t *n, char **err) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) { *err = mesh_fmt("/proc/net/dev: %s", strerror(errno)); return -1; }
    mesh_buf b = MESH_BUF_INIT;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) mesh_buf_put(&b, chunk, got);
    fclose(f);
    int r = mesh_nic_parse_proc_net_dev(mesh_buf_str(&b), out, n, err);
    mesh_buf_free(&b);
    return r;
}
#elif defined(__APPLE__)
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/sysctl.h>
/* sysctl {CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0}: one RTM_IFINFO2 message
 * (if_msghdr2, with the 64-bit if_data64) per interface. getifaddrs' AF_LINK
 * if_data has only 32-bit counters, which wrap every 4 GiB. Transmit drops are
 * the send queue's (ifm_snd_drops, an int). */
int mesh_nic_read(mesh_nic_raw **out, size_t *n, char **err) {
    int mib[6] = {CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0};
    char *buf = NULL;
    size_t got = 0;
    for (int tries = 0; tries < 4 && !buf; tries++) {
        size_t len = 0;
        if (sysctl(mib, 6, NULL, &len, NULL, 0) != 0) { *err = mesh_fmt("sysctl NET_RT_IFLIST2 size: %s", strerror(errno)); return -1; }
        buf = mesh_xmalloc(len + 4096);
        got = len + 4096;
        if (sysctl(mib, 6, buf, &got, NULL, 0) == 0) break;
        if (errno != ENOMEM) { *err = mesh_fmt("sysctl NET_RT_IFLIST2: %s", strerror(errno)); free(buf); return -1; }
        free(buf);
        buf = NULL;
    }
    if (!buf) { *err = mesh_xstrdup("sysctl NET_RT_IFLIST2: the list kept growing"); return -1; }
    *out = NULL;
    *n = 0;
    size_t cap = 0, at = 0;
    while (at + 4 <= got) {
        size_t msglen = (size_t)((unsigned char)buf[at] | (unsigned char)buf[at + 1] << 8);
        if (msglen == 0 || at + msglen > got) { *err = mesh_fmt("sysctl NET_RT_IFLIST2: bad message length %zu at %zu", msglen, at); free(buf); free(*out); return -1; }
        if (buf[at + 3] == RTM_IFINFO2) {
            struct if_msghdr2 m;
            if (msglen < sizeof m) { *err = mesh_fmt("sysctl NET_RT_IFLIST2: RTM_IFINFO2 of %zu bytes", msglen); free(buf); free(*out); return -1; }
            memcpy(&m, buf + at, sizeof m);
            char name[IF_NAMESIZE];
            if (if_indextoname(m.ifm_index, name)) {
                if (*n == cap) { cap = cap ? 2 * cap : 16; *out = mesh_xrealloc(*out, cap * sizeof **out); }
                mesh_nic_raw *r = &(*out)[(*n)++];
                memset(r, 0, sizeof *r);
                snprintf(r->name, sizeof r->name, "%s", name);
                const uint64_t v[8] = {m.ifm_data.ifi_ibytes, m.ifm_data.ifi_obytes, m.ifm_data.ifi_ipackets, m.ifm_data.ifi_opackets,
                                       m.ifm_data.ifi_ierrors, m.ifm_data.ifi_oerrors, m.ifm_data.ifi_iqdrops, (uint64_t)(uint32_t)m.ifm_snd_drops};
                for (int i = 0; i < 8; i++) { r->v[i] = v[i]; r->bits[i] = i == 7 ? 32 : 64; }
            }
        }
        at += msglen;
    }
    free(buf);
    if (*n == 0) { *err = mesh_xstrdup("sysctl NET_RT_IFLIST2 lists no interface"); return -1; }
    return 0;
}
#else
int mesh_nic_read(mesh_nic_raw **out, size_t *n, char **err) {
    *out = NULL;
    *n = 0;
    *err = mesh_xstrdup("interface counters: not implemented on this OS");
    return -1;
}
#endif

int mesh_nic_iface_of(const char *ip, char name[MESH_NIC_NAME_MAX], char **err) {
    unsigned char want[16];
    int fam = AF_INET;
    if (inet_pton(AF_INET, ip, want) != 1) {
        fam = AF_INET6;
        if (inet_pton(AF_INET6, ip, want) != 1) { *err = mesh_fmt("invalid IP address syntax: %s", ip); return -1; }
    }
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list) != 0) { *err = mesh_fmt("getifaddrs: %s", strerror(errno)); return -1; }
    int found = 0;
    for (struct ifaddrs *a = list; a && !found; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != fam) continue;
        const void *addr = fam == AF_INET ? (const void *)&((struct sockaddr_in *)(void *)a->ifa_addr)->sin_addr
                                          : (const void *)&((struct sockaddr_in6 *)(void *)a->ifa_addr)->sin6_addr;
        if (memcmp(addr, want, fam == AF_INET ? 4 : 16) == 0) {
            snprintf(name, MESH_NIC_NAME_MAX, "%s", a->ifa_name);
            found = 1;
        }
    }
    freeifaddrs(list);
    if (!found) {
        char canon[64];
        inet_ntop(fam, want, canon, sizeof canon);
        *err = mesh_fmt("no interface holds %s", canon);
        return -1;
    }
    return 0;
}

void mesh_nics_sample(mesh_nics *s, const mesh_nic_raw *raw, size_t n, mesh_nic_now **now, size_t *nnow) {
    int first = s->n == 0;
    *now = mesh_xcalloc(n ? n : 1, sizeof **now);
    *nnow = n;
    uint32_t counted = 0;
    char *seen = mesh_xcalloc(s->n ? s->n : 1, 1);
    for (size_t i = 0; i < n; i++) {
        const mesh_nic_raw *r = &raw[i];
        int include = !mesh_nic_virtual((const uint8_t *)r->name, strlen(r->name));
        counted += (uint32_t)include;
        mesh_nic_entry_ *e = NULL;
        for (size_t j = 0; j < s->n; j++) if (strcmp(s->ext[j].name, r->name) == 0) { e = &s->ext[j]; seen[j] = 1; break; }
        if (e) {
            for (int c = 0; c < 8; c++) {
                uint64_t d = mesh_nic_ext_update(&e->e[c], r->v[c], r->bits[c]);
                if (include) s->total[c] += d;
            }
        } else {
            s->ext = mesh_xrealloc(s->ext, (s->n + 1) * sizeof *s->ext);
            seen = mesh_xrealloc(seen, s->n + 1);
            seen[s->n] = 1;
            e = &s->ext[s->n++];
            snprintf(e->name, sizeof e->name, "%s", r->name);
            for (int c = 0; c < 8; c++) e->e[c] = mesh_nic_ext_new(r->v[c]);
            if (include && first) for (int c = 0; c < 8; c++) s->total[c] += r->v[c];
        }
        snprintf((*now)[i].name, sizeof (*now)[i].name, "%s", r->name);
        for (int c = 0; c < 8; c++) (*now)[i].v[c] = e->e[c].value;
    }
    /* Interfaces that vanished: forget them (a re-created one starts afresh). */
    size_t k = 0;
    for (size_t j = 0; j < s->n; j++) if (seen[j]) s->ext[k++] = s->ext[j];
    s->n = k;
    free(seen);
    s->counted = counted;
}

const uint64_t *mesh_nic_lookup(const mesh_nic_now *now, size_t n, const char *name) {
    for (size_t i = 0; i < n; i++) if (strcmp(now[i].name, name) == 0) return now[i].v;
    return NULL;
}

void mesh_nics_free(mesh_nics *s) {
    free(s->ext);
    s->ext = NULL;
    s->n = 0;
}

void mesh_nic_record(uint8_t out[MESH_NIC_BYTES], const char *name, const uint64_t v[8], uint32_t links, uint32_t count) {
    memset(out, 0, MESH_NIC_BYTES);
    size_t n = strlen(name);
    if (n > 15) n = 15;
    memcpy(out + MESH_NI_NAME, name, n);
    for (int i = 0; i < 8; i++)
        for (int b = 0; b < 8; b++) out[MESH_NIC_COUNTERS[i] + (size_t)b] = (uint8_t)(v[i] >> (8 * b));
    for (int b = 0; b < 4; b++) {
        out[MESH_NI_LINKS + (size_t)b] = (uint8_t)(links >> (8 * b));
        out[MESH_NI_COUNT + (size_t)b] = (uint8_t)(count >> (8 * b));
    }
}
