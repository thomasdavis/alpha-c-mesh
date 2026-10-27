/* The HTTP port: read a head (2 s, at most the http program's request window), route, answer, close. */
#include "mesh/http_relay.h"
#include "mesh/abi.h"
#include "mesh/buf.h"
#include "mesh/control.h"
#include "mesh/infer_request.h"
#include "mesh/inferd.h"
#include "mesh/log.h"
#include "mesh/rsfmt.h"
#include "mesh/views.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_CLIENTS 64
#define HEAD_TIME_MS 2000
#define WRITE_TIME_S 5

static bool has_blank(const mesh_buf *b) {
    for (size_t i = 0; i + 4 <= b->len; i++)
        if (b->data[i] == '\r' && b->data[i + 1] == '\n' && b->data[i + 2] == '\r' && b->data[i + 3] == '\n') return true;
    return false;
}

/* The request head: up to the http program's request window, until a blank line, EOF or HEAD_TIME in all. */
static void read_head(int fd, mesh_buf *head) {
    size_t max = MESH_HTTP_REQ_MAX;
    int64_t deadline = mesh_mono_ms() + HEAD_TIME_MS;
    char buf[512];
    while (head->len < max && !has_blank(head)) {
        int64_t left = deadline - mesh_mono_ms();
        if (left <= 0) break;
        struct pollfd p = {fd, POLLIN, 0};
        int r = poll(&p, 1, (int)left);
        if (r <= 0) break;
        ssize_t k = read(fd, buf, sizeof buf);
        if (k <= 0) break;
        mesh_buf_put(head, buf, (size_t)k);
    }
    mesh_buf_truncate(head, max);
}

bool mesh_http_view_route(const char *first, const char **view, const char **query, size_t *qlen) {
    if (strncmp(first, "GET ", 4) != 0) return false;
    const char *t = first + 4, *sp = strchr(t, ' ');
    size_t tn = sp ? (size_t)(sp - t) : strlen(t);
    const char *q = memchr(t, '?', tn);
    size_t pn = q ? (size_t)(q - t) : tn;
    static const char *const views[3] = {"fleet", "jobs", "pipes"};
    for (int i = 0; i < 3; i++) {
        size_t k = strlen(views[i]);
        if (pn == k + 1 && t[0] == '/' && strncmp(t + 1, views[i], k) == 0) {
            *view = views[i];
            *query = q ? q + 1 : t + tn;
            *qlen = q ? tn - pn - 1 : 0;
            return true;
        }
    }
    return false;
}

bool mesh_http_render_route(const char *first, const char **what, char **job) {
    if (strncmp(first, "GET ", 4) != 0) return false;
    const char *t = first + 4, *sp = strchr(t, ' ');
    size_t tn = sp ? (size_t)(sp - t) : strlen(t);
    const char *q = memchr(t, '?', tn);
    if (!q) return false;
    size_t pn = (size_t)(q - t);
    if (pn == 7 && strncmp(t, "/render", 7) == 0) *what = "status";
    else if (pn == 11 && strncmp(t, "/render.bmp", 11) == 0) *what = "bmp";
    else if (pn == 11 && strncmp(t, "/render.ppm", 11) == 0) *what = "ppm";
    else return false;
    const char *p = q + 1, *end = t + tn;
    while (p <= end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *e = amp ? amp : end;
        if ((size_t)(e - p) >= 4 && strncmp(p, "job=", 4) == 0) {
            *job = e - p == 4 ? mesh_xstrdup("latest") : mesh_xstrndup(p + 4, (size_t)(e - p - 4));
            return true;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return false;
}

/* The head's first line, lossily as text. */
static char *first_line(const uint8_t *b, size_t n) {
    size_t k = 0;
    while (k < n && b[k] != '\r' && b[k] != '\n') k++;
    mesh_buf t = MESH_BUF_INIT;
    mesh_rs_lossy(&t, b, k);
    return mesh_buf_take(&t);
}

static size_t write_all(int fd, const void *p, size_t n) {
    const char *c = p;
    size_t left = n;
    while (left) {
        ssize_t w = write(fd, c, left);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return 0;
        c += w;
        left -= (size_t)w;
    }
    return n;
}

static void timing(mesh_buf *l, int64_t t0) {
    mesh_rs_fixed(l, (double)(mesh_mono_ns() - t0) / 1e6, 2);
}

void mesh_http_client(mesh_shared *sh, int fd) {
    struct sockaddr_storage peer;
    socklen_t pl = sizeof peer;
    char who[80] = "";
    if (getpeername(fd, (struct sockaddr *)&peer, &pl) == 0 && peer.ss_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&peer;
        char ip[32];
        inet_ntop(AF_INET, &a->sin_addr, ip, sizeof ip);
        snprintf(who, sizeof who, "%s:%u", ip, ntohs(a->sin_port));
    }
    struct timeval wt = {WRITE_TIME_S, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wt, sizeof wt);
    mesh_buf head = MESH_BUF_INIT;
    read_head(fd, &head);
    if (!head.len) { mesh_net_counters_http(&sh->host.net, 0, 0); close(fd); return; }
    int64_t t0 = mesh_mono_ns();
    char *first = first_line((uint8_t *)head.data, head.len);
    const char *view, *query;
    size_t qlen;
    const char *what;
    char *job = NULL;
    mesh_buf resp = MESH_BUF_INIT, log = MESH_BUF_INIT;
    if (mesh_http_view_route(first, &view, &query, &qlen)) {
        /* GET /fleet, /jobs, /pipes [?format=json|text]: the fleet's views, from meshd. */
        char *q = mesh_xstrndup(query, qlen);
        bool json = false;
        char *err = NULL;
        mesh_buf body = MESH_BUF_INIT;
        const char *code, *ctype;
        if (mesh_views_format_of(q, &json, &err) == 0 && mesh_control_view(sh, view, json, &body, &err) == 0) {
            code = "200 OK";
            ctype = json ? "application/json" : "text/plain; charset=utf-8";
        } else {
            code = "400 Bad Request";
            ctype = "text/plain; charset=utf-8";
            mesh_buf_clear(&body);
            mesh_buf_printf(&body, "meshd: %s\n", err);
            free(err);
        }
        mesh_buf_printf(&resp, "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n\r\n", code, ctype, body.len);
        mesh_buf_put(&resp, body.data ? body.data : "", body.len);
        size_t sent = write_all(fd, resp.data, resp.len);
        mesh_net_counters_http(&sh->host.net, head.len, sent);
        mesh_buf_printf(&log, "http %s \"%s\" -> %.3s %zu bytes ", who, first, code, resp.len);
        timing(&log, t0);
        mesh_buf_printf(&log, " ms (meshd: the %s view)", view);
        mesh_info("%s", mesh_buf_str(&log));
        mesh_buf_free(&body);
        free(q);
    } else if (strncmp(first, "GET /infer?", 11) == 0) {
        /* GET /infer?prompt=..&max=..&mode=..&prefill=..&decode=..&mono=..: GPT-2 across the fleet. */
        const char *qs = first + 11, *sp = strchr(qs, ' ');
        char *q = sp ? mesh_xstrndup(qs, (size_t)(sp - qs)) : mesh_xstrdup(qs);
        mesh_infer_request r;
        mesh_infer_msg res;
        bool full = false;
        char *err = NULL;
        mesh_buf body = MESH_BUF_INIT;
        const char *code;
        if (mesh_infer_from_query(q, &r, &full, &err) == 0 && mesh_inferd_run(sh, &r, &res, &err) == 0) {
            code = "200 OK";
            if (full) mesh_infer_text(&body, &res);
            else mesh_buf_printf(&body, "%s\n", res.body);
            mesh_infer_msg_free(&res);
        } else {
            code = "400 Bad Request";
            mesh_buf_printf(&body, "meshd: infer: %s\n", err);
            free(err);
        }
        mesh_infer_request_free(&r);
        mesh_buf_printf(&resp, "HTTP/1.0 %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n\r\n", code,
                        body.len);
        mesh_buf_put(&resp, body.data ? body.data : "", body.len);
        size_t sent = write_all(fd, resp.data, resp.len);
        mesh_net_counters_http(&sh->host.net, head.len, sent);
        mesh_buf_printf(&log, "http %s \"%s\" -> %.3s %zu bytes ", who, first, code, resp.len);
        timing(&log, t0);
        mesh_buf_puts(&log, " ms (meshd: infer)");
        mesh_info("%s", mesh_buf_str(&log));
        mesh_buf_free(&body);
        free(q);
    } else if (mesh_http_render_route(first, &what, &job)) {
        char *words[2] = {job, (char *)what};
        uint8_t *b = NULL;
        size_t n = 0;
        char *err = NULL;
        bool ok = mesh_control_render ? mesh_control_render(sh, words, 2, &b, &n, &err) : (err = mesh_xstrdup("the blob thread is gone"), false);
        const char *code = ok ? "200 OK" : "404 Not Found";
        const char *ctype = !ok ? "text/plain; charset=utf-8" : strcmp(what, "bmp") == 0 ? "image/bmp" : strcmp(what, "ppm") == 0 ? "image/x-portable-pixmap"
                                                                                                                                   : "text/plain; charset=utf-8";
        mesh_buf body = MESH_BUF_INIT;
        if (ok) mesh_buf_put(&body, b ? b : (uint8_t *)"", n);
        else mesh_buf_printf(&body, "meshd: %s\n", err);
        mesh_buf_printf(&resp, "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n\r\n", code, ctype, body.len);
        mesh_buf_put(&resp, body.data ? body.data : "", body.len);
        size_t sent = write_all(fd, resp.data, resp.len);
        mesh_net_counters_http(&sh->host.net, head.len, sent);
        mesh_buf_printf(&log, "http %s \"%s\" -> %.3s %zu bytes ", who, first, code, resp.len);
        timing(&log, t0);
        mesh_buf_puts(&log, " ms (meshd: the assembled image)");
        mesh_info("%s", mesh_buf_str(&log));
        mesh_buf_free(&body);
        free(b);
        free(err);
        free(job);
    } else {
        uint8_t *r = NULL;
        size_t rn = 0;
        char *err = NULL;
        if (mesh_shared_http(sh, (uint8_t *)head.data, head.len, &r, &rn, &err)) {
            size_t sent = write_all(fd, r, rn);
            mesh_net_counters_http(&sh->host.net, head.len, sent);
            char *rl = first_line(r, rn);
            mesh_buf_printf(&log, "http %s \"%s\" -> \"%s\" %zu bytes ", who, first, rl, rn);
            timing(&log, t0);
            mesh_buf_puts(&log, " ms");
            mesh_info("%s", mesh_buf_str(&log));
            free(rl);
            free(r);
        } else {
            /* No GPU response (no http program, or it failed): say so, never hang the client. */
            mesh_error("http %s \"%s\": %s", who, first, err);
            char *body = mesh_buf_fmt("meshd: %s\n", err);
            mesh_buf_printf(&resp, "HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
            size_t sent = write_all(fd, resp.data, resp.len);
            mesh_net_counters_http(&sh->host.net, head.len, sent);
            free(body);
            free(err);
        }
    }
    shutdown(fd, SHUT_RDWR);
    close(fd);
    mesh_buf_free(&resp);
    mesh_buf_free(&log);
    mesh_buf_free(&head);
    free(first);
}

typedef struct {
    mesh_shared *sh;
    int fd;
} client_arg;

static _Atomic int clients;

static void *client_thread(void *p) {
    client_arg *a = p;
    mesh_http_client(a->sh, a->fd);
    atomic_fetch_sub(&clients, 1);
    free(a);
    return NULL;
}

void mesh_http_serve(mesh_shared *sh, int listen_fd) {
    for (;;) {
        struct sockaddr_storage peer;
        socklen_t pl = sizeof peer;
        int c = accept(listen_fd, (struct sockaddr *)&peer, &pl);
        if (c < 0) {
            if (errno == EINTR) continue;
            mesh_error("http accept: %s (os error %d)", strerror(errno), errno);
            if (errno == EBADF || errno == EINVAL) return;
            continue;
        }
        if (atomic_load(&clients) >= MAX_CLIENTS) {
            char who[80] = "";
            if (peer.ss_family == AF_INET) {
                struct sockaddr_in *a = (struct sockaddr_in *)&peer;
                char ip[32];
                inet_ntop(AF_INET, &a->sin_addr, ip, sizeof ip);
                snprintf(who, sizeof who, "%s:%u", ip, ntohs(a->sin_port));
            }
            mesh_error("http: %d clients already; closing %s", MAX_CLIENTS, who);
            close(c);
            continue;
        }
        atomic_fetch_add(&clients, 1);
        client_arg *a = mesh_xmalloc(sizeof *a);
        a->sh = sh;
        a->fd = c;
        pthread_t th;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, client_thread, a) != 0) {
            atomic_fetch_sub(&clients, 1);
            mesh_error("http: cannot spawn a client thread: %s", strerror(errno));
            close(c);
            free(a);
        }
        pthread_attr_destroy(&at);
    }
}
