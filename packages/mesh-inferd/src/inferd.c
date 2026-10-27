/* inferd.rs: the pair, the call, the answer. */
#include "mesh/inferd.h"
#include "mesh/log.h"
#include "mesh/manifest.h"
#include "mesh/prog_table.h"
#include "mesh/rsfmt.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

size_t mesh_inferd_members_of(mesh_shared *sh, const char *name, uint32_t *out) {
    if (!sh->peer) {
        uint32_t s;
        if (mesh_pt_find(sh->mb, name, &s)) { out[0] = 0; return 1; }
        return 0;
    }
    char *text = mesh_shared_text(sh, &sh->fleet_text);
    size_t n = 0;
    char *save = NULL;
    for (char *l = strtok_r(text, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
        char *w[64];
        size_t nw = 0;
        char *ws = NULL;
        for (char *t = strtok_r(l, " \t\r", &ws); t && nw < 64; t = strtok_r(NULL, " \t\r", &ws)) w[nw++] = t;
        if (nw > 2 && strcmp(w[0], "program") == 0 && strcmp(w[1], name) == 0) {
            size_t i = 0;
            while (i < nw && strcmp(w[i], "at") != 0) i++;
            if (i >= nw || i + 1 >= nw) break;
            if (strcmp(w[i + 1], "all") == 0) { for (uint32_t k = 0; k < sh->n; k++) out[n++] = k; break; }
            char *cs = NULL;
            for (char *x = strtok_r(w[i + 1], ",", &cs); x; x = strtok_r(NULL, ",", &cs)) {
                uint32_t v;
                if (mesh_rs_parse_u32(x, strlen(x), &v) == 0 && n < MESH_MAX_SYSTEMS) out[n++] = v;
            }
            break;
        }
    }
    free(text);
    return n;
}

static int param_of(mesh_shared *sh, const char *name, uint32_t *param, char **err) {
    char *dir = mesh_buf_fmt("%s/%s", sh->programs, name);
    mesh_manifest m;
    char *ptx = NULL, *e = NULL;
    bool ok = mesh_manifest_load(dir, &m, &ptx, &e);
    free(dir);
    free(ptx);
    if (!ok) { *err = mesh_buf_fmt("%s: %s", name, e); free(e); return -1; }
    *param = m.param;
    mesh_manifest_free(&m);
    return 0;
}

/* connect_timeout (3 s) to host:port; the fd or -1 with *err (std's words). */
static int dial(const char *addr, int timeout_ms, char **err) {
    const char *c = strrchr(addr, ':');
    if (!c) { *err = mesh_buf_fmt("%s: invalid socket address syntax", addr); return -1; }
    char host[256];
    snprintf(host, sizeof host, "%.*s", (int)(c - addr), addr);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)atoi(c + 1));
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) { *err = mesh_buf_fmt("%s: invalid socket address syntax", addr); return -1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { *err = mesh_buf_fmt("%s (os error %d)", strerror(errno), errno); return -1; }
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (r != 0 && errno == EINPROGRESS) {
        struct pollfd p = {fd, POLLOUT, 0};
        r = poll(&p, 1, timeout_ms);
        if (r == 0) { close(fd); *err = mesh_xstrdup("connection timed out"); return -1; }
        int so = 0;
        socklen_t sl = sizeof so;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl);
        if (so) { close(fd); *err = mesh_buf_fmt("%s (os error %d)", strerror(so), so); return -1; }
    } else if (r != 0) {
        int e = errno;
        close(fd);
        *err = mesh_buf_fmt("%s (os error %d)", strerror(e), e);
        return -1;
    }
    fcntl(fd, F_SETFL, fl);
    return fd;
}

int mesh_inferd_run(mesh_shared *sh, const mesh_infer_request *r, mesh_infer_msg *res, char **err) {
    if (!sh->has_svc) { *err = mesh_xstrdup("this meshd has no service addresses"); return -1; }
    const mesh_infer_svc *svc = &sh->svc;
    uint32_t me = svc->me;
    int64_t t0 = mesh_mono_ns();
    uint8_t kind;
    uint32_t target;
    const char *prog;
    mesh_infer_msg msg;
    mesh_infer_msg_init(&msg);
    char addr[300];
    if (r->has_prefill && r->has_decode) {
        uint32_t pp;
        if (param_of(sh, MESH_INFER_PROGRAM_PREFILL, &pp, err)) { mesh_infer_msg_free(&msg); return -1; }
        mesh_infer_svc_addr(svc, r->decode, r->prefill, pp, addr);
        mesh_infer_msg_set(&msg, "prefill", addr);
        mesh_infer_msg_setf(&msg, "prefill_sys", "%u", r->prefill);
        mesh_infer_msg_setf(&msg, "decode_sys", "%u", r->decode);
        kind = MESH_INFER_K_DECODE;
        target = r->decode;
        prog = MESH_INFER_PROGRAM_DECODE;
    } else if (r->has_mono) {
        kind = MESH_INFER_K_GEN;
        target = r->mono;
        prog = MESH_INFER_PROGRAM_MONO;
        mesh_infer_msg_setf(&msg, "prefill_sys", "%u", r->mono);
        mesh_infer_msg_setf(&msg, "decode_sys", "%u", r->mono);
    } else {
        /* The scheduler's placement: a prefill member, and a decode member other than it (this one if it decodes). */
        uint32_t ps[MESH_MAX_SYSTEMS], ds[MESH_MAX_SYSTEMS];
        size_t np = mesh_inferd_members_of(sh, MESH_INFER_PROGRAM_PREFILL, ps), nd = mesh_inferd_members_of(sh, MESH_INFER_PROGRAM_DECODE, ds);
        bool has_d = false;
        uint32_t dsel = 0;
        if (np) {
            for (size_t i = 0; i < nd && !has_d; i++) if (ds[i] == me && ds[i] != ps[0]) { dsel = ds[i]; has_d = true; }
            for (size_t i = 0; i < nd && !has_d; i++) if (ds[i] != ps[0]) { dsel = ds[i]; has_d = true; }
        }
        if (np && has_d) {
            uint32_t pp;
            if (param_of(sh, MESH_INFER_PROGRAM_PREFILL, &pp, err)) { mesh_infer_msg_free(&msg); return -1; }
            mesh_infer_svc_addr(svc, dsel, ps[0], pp, addr);
            mesh_infer_msg_set(&msg, "prefill", addr);
            mesh_infer_msg_setf(&msg, "prefill_sys", "%u", ps[0]);
            mesh_infer_msg_setf(&msg, "decode_sys", "%u", dsel);
            kind = MESH_INFER_K_DECODE;
            target = dsel;
            prog = MESH_INFER_PROGRAM_DECODE;
        } else {
            uint32_t mono[MESH_MAX_SYSTEMS];
            size_t nm = mesh_inferd_members_of(sh, MESH_INFER_PROGRAM_MONO, mono);
            bool here = false;
            for (size_t i = 0; i < nm; i++) here |= mono[i] == me;
            if (!here && !nm) { *err = mesh_xstrdup("no gpt2, gpt2-prefill or gpt2-decode is placed on any member"); mesh_infer_msg_free(&msg); return -1; }
            target = here ? me : mono[0];
            kind = MESH_INFER_K_GEN;
            prog = MESH_INFER_PROGRAM_MONO;
            mesh_infer_msg_setf(&msg, "prefill_sys", "%u", target);
            mesh_infer_msg_setf(&msg, "decode_sys", "%u", target);
        }
    }
    if (target >= svc->n) { *err = mesh_buf_fmt("member %u: the cluster has %u", target, svc->n); mesh_infer_msg_free(&msg); return -1; }
    mesh_infer_msg_set(&msg, "mode", r->mode);
    mesh_infer_msg_setf(&msg, "max", "%u", r->max);
    if (r->has_len) mesh_infer_msg_setf(&msg, "len", "%u", r->len);
    if (r->has_repeat) mesh_infer_msg_setf(&msg, "repeat", "%u", r->repeat);
    mesh_infer_msg_set_body(&msg, r->prompt);
    uint32_t param;
    if (param_of(sh, prog, &param, err)) { mesh_infer_msg_free(&msg); return -1; }
    mesh_infer_svc_addr(svc, me, target, param, addr);
    char *e = NULL;
    int fd = dial(addr, 3000, &e);
    if (fd < 0) {
        *err = mesh_buf_fmt("%s on member %u (%s): %s", prog, target, addr, e);
        free(e);
        mesh_infer_msg_free(&msg);
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    struct timeval tv = {MESH_INFER_REQUEST_WAIT_MS / 1000, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    mesh_buf enc = MESH_BUF_INIT;
    mesh_infer_msg_encode(&msg, &enc);
    if (mesh_infer_write_frame(fd, kind, enc.data, enc.len) != 0) {
        *err = mesh_buf_fmt("%s: %s (os error %d)", addr, strerror(errno), errno);
        mesh_buf_free(&enc);
        close(fd);
        mesh_infer_msg_free(&msg);
        return -1;
    }
    mesh_buf_free(&enc);
    uint8_t k;
    mesh_buf body = MESH_BUF_INIT;
    if (mesh_infer_read_frame(fd, &k, &body, NULL, &e) != 0) {
        *err = mesh_buf_fmt("%s on member %u (%s): %s", prog, target, addr, e);
        free(e);
        close(fd);
        mesh_buf_free(&body);
        mesh_infer_msg_free(&msg);
        return -1;
    }
    close(fd);
    if (mesh_infer_msg_decode(body.data ? body.data : "", body.len, res, err) != 0) { mesh_buf_free(&body); mesh_infer_msg_free(&msg); return -1; }
    mesh_buf_free(&body);
    if (k == MESH_INFER_K_ERROR) {
        /* res.body.trim() */
        const char *s = res->body;
        size_t n = strlen(s);
        while (n && (s[n - 1] == ' ' || (s[n - 1] >= 9 && s[n - 1] <= 13))) n--;
        while (n && (*s == ' ' || (*s >= 9 && *s <= 13))) { s++; n--; }
        *err = mesh_buf_fmt("%s on member %u: %.*s", prog, target, (int)n, s);
        mesh_infer_msg_free(res);
        mesh_infer_msg_free(&msg);
        return -1;
    }
    if (k != MESH_INFER_K_RESULT) {
        *err = mesh_buf_fmt("%s on member %u: answered a frame of kind %u", prog, target, k);
        mesh_infer_msg_free(res);
        mesh_infer_msg_free(&msg);
        return -1;
    }
    mesh_infer_msg_setf(res, "coordinator", "%u", me);
    const char *keys[2] = {"prefill_sys", "decode_sys"};
    for (int i = 0; i < 2; i++) {
        const char *v = mesh_infer_msg_get(&msg, keys[i]);
        if (v) mesh_infer_msg_set(res, keys[i], v);
    }
    mesh_buf ms = MESH_BUF_INIT;
    mesh_rs_fixed(&ms, (double)(mesh_mono_ns() - t0) / 1e6, 2);
    mesh_infer_msg_set(res, "request_ms", mesh_buf_str(&ms));
    mesh_buf_free(&ms);
    mesh_infer_msg_set(res, "program", prog);
    const char *g = mesh_infer_msg_get(res, "gen_tokens"), *tt = mesh_infer_msg_get(res, "ttft_ms"), *ts = mesh_infer_msg_get(res, "tok_s");
    if (kind == MESH_INFER_K_DECODE) {
        const char *ps = mesh_infer_msg_get(res, "prefill_sys"), *dsy = mesh_infer_msg_get(res, "decode_sys");
        mesh_info("infer: %s prefill %s decode %s via %s on %u: %s tokens, ttft %s ms, %s tok/s", r->mode, ps ? ps : "?", dsy ? dsy : "?", prog, target,
                  g ? g : "?", tt ? tt : "?", ts ? ts : "?");
    } else {
        mesh_info("infer: %s mono %u via %s on %u: %s tokens, ttft %s ms, %s tok/s", r->mode, target, prog, target, g ? g : "?", tt ? tt : "?", ts ? ts : "?");
    }
    mesh_infer_msg_free(&msg);
    return 0;
}
