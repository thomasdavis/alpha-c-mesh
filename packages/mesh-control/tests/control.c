/* The control port over TCP against a stand-in driver: every reply shape (lines, framed bodies, errors),
 * the texts (programs, pipes, views, cluster, peek), req bodies, the line limit, and control.rs's
 * an_idle_client_is_closed. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/abi.h"
#include "mesh/check.h"
#include "mesh/control.h"
#include "mesh/infer_request.h"
#include "mesh/log.h"
#include "mesh/manifest.h"
#include "mesh/manifest_encode.h"
#include "mesh/prog_table.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static mesh_shared *SH;

static void *driver(void *arg) {
    (void)arg;
    mesh_cmd c;
    while (mesh_chan_recv(SH->cmd, &c, -1) == MESH_CHAN_OK) {
        switch (c.k) {
        case MESH_CMD_LOAD: mesh_reply_ok(c.reply, "slot 0 blink 12"); break;
        case MESH_CMD_TASK: mesh_reply_value(c.reply, c.arg * 3 + 7); break;
        case MESH_CMD_REQ: mesh_reply_bytes(c.reply, c.bytes, c.len); break;
        case MESH_CMD_INPUT: mesh_reply_okf(c.reply, "%zu bytes", c.len); break;
        case MESH_CMD_UNLOAD: mesh_reply_ok(c.reply, ""); break;
        case MESH_CMD_PROG_RUN: mesh_reply_errf(c.reply, "no program \"%s\" (`programs` lists them)", c.name); break;
        default: mesh_reply_err(c.reply, "unexpected"); break;
        }
        c.reply = NULL;
        mesh_cmd_free(&c);
    }
    return NULL;
}

static int conn(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) return -1;
    return fd;
}

static int FD;
static void send_s(const char *s) {
    size_t n = strlen(s);
    if (write(FD, s, n) != (ssize_t)n) abort();
}
static bool read_n(int fd, void *b, size_t n) {
    char *p = b;
    while (n) {
        ssize_t k = read(fd, p, n);
        if (k <= 0) return false;
        p += k;
        n -= (size_t)k;
    }
    return true;
}
/* One reply line. */
static char *rline(void) {
    static char b[8192];
    size_t i = 0;
    while (i < sizeof b - 1 && read(FD, b + i, 1) == 1) if (b[i++] == '\n') break;
    b[i] = 0;
    return b;
}
/* One framed reply, as text. */
static char *rframe(size_t *len) {
    uint8_t h[4];
    if (!read_n(FD, h, 4)) return NULL;
    size_t n = (size_t)h[0] | (size_t)h[1] << 8 | (size_t)h[2] << 16 | (size_t)h[3] << 24;
    char *b = malloc(n + 1);
    read_n(FD, b, n);
    b[n] = 0;
    if (len) *len = n;
    return b;
}

static int lfd;
static void *serve(void *arg) {
    (void)arg;
    mesh_control_serve(SH, lfd);
    return NULL;
}

int main(void) {
    mesh_log_set_fd(-1);
    static _Atomic bool quit;
    mesh_mailbox mb = mesh_mailbox_test();
    SH = mesh_shared_new(mb, "/nonexistent-catalog", false, 0, 1, &quit);
    mesh_chan_close_rx(SH->jobs); /* no jobs thread here */
    /* A loaded wasm program in slot 0. */
    mesh_manifest m;
    char *err = NULL;
    const char *mt = "name = \"hello\"\nabi = 6\nview_bytes = 0x1000\nnodes = 2\ncounters = [\"tasks\"]\nbackends = [\"wasm\"]";
    CHECK(mesh_manifest_parse(mt, strlen(mt), &m, &err));
    mesh_image im = {0x100000, 0, 0x1234, 0, 0, MESH_RT_ABI, 1};
    uint8_t e[MESH_PT_BYTES], x[MESH_PX_BYTES];
    mesh_manifest_encode_entry(&m, &im, e);
    mesh_manifest_encode_ext(&m, x);
    x[MESH_PX_EXEC] = MESH_BACKEND_WASM;
    mesh_pt_write_entry(mb, 0, e, x, MESH_ST_RUNNING);
    mesh_mailbox_set(mb, 0x100000, 0x4f4c4548u);
    mesh_manifest_free(&m);
    pthread_t dt, st;
    pthread_create(&dt, NULL, driver, NULL);
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(lfd, (struct sockaddr *)&sa, sizeof sa);
    socklen_t sl = sizeof sa;
    getsockname(lfd, (struct sockaddr *)&sa, &sl);
    listen(lfd, 8);
    int port = ntohs(sa.sin_port);
    pthread_create(&st, NULL, serve, NULL);
    FD = conn(port);
    CHECK(FD >= 0);

    send_s("load hello\n");
    CHECK_S(rline(), "ok slot 0 blink 12\n");
    send_s("task hello 5 0x10\n");
    CHECK_S(rline(), "ok 55\n");
    send_s("task x\n");
    CHECK_S(rline(), "err usage: task <name> <op> <arg>\n");
    send_s("  unload hello  \r\n");
    CHECK_S(rline(), "ok\n");
    send_s("start nope\n");
    CHECK_S(rline(), "err no program \"nope\" (`programs` lists them)\n");
    send_s("start a b\n");
    CHECK_S(rline(), "err unknown or malformed request \"start a b\"\n");
    send_s("frobnicate\n");
    CHECK_S(rline(), "err unknown or malformed request \"frobnicate\"\n");
    send_s("\n");
    CHECK_S(rline(), "err empty request\n");
    send_s("fleet\n");
    CHECK_S(rline(), "err not in a fleet (single meshd: --cluster / --peer)\n");
    send_s("jobs\n");
    CHECK_S(rline(), "err the jobs thread is gone\n");
    send_s("input hello abc\n");
    CHECK_S(rline(), "ok 3 bytes\n");
    send_s("input hello\n");
    CHECK_S(rline(), "err usage: input <name> <bytes>\n");
    send_s("key\n");
    CHECK_S(rline(), "err usage: key <k>\n");
    send_s("place\n");
    CHECK_S(rline(), "err usage: place <name> [local | pin <k> | split [auto | a,b,.. | a:n,b:m,..]]\n");

    char *f;
    send_s("programs\n");
    f = rframe(NULL);
    CHECK_S(f, "0 hello running 2 1 on_demand 0.0 0000000000001234 0 0x100000+0x1000 kind wasm lanes 0 cpu 0.00 rss_mb 0.0\n"
               "fields hello\ncounters hello tasks - - -\n");
    free(f);
    send_s("req hello 5\nabcde");
    size_t n;
    f = rframe(&n);
    CHECK(n == 5 && memcmp(f, "abcde", 5) == 0);
    free(f);
    send_s("req hello x\n");
    f = rframe(NULL);
    CHECK_S(f, "err usage: req <name> <len>, then the bytes\n");
    free(f);
    send_s("req hello 2000000\n");
    f = rframe(NULL);
    CHECK_S(f, "err req: at most 1048576 bytes\n");
    free(f);
    send_s("peek hello 0 4\n");
    f = rframe(&n);
    CHECK(n == 4 && memcmp(f, "HELO", 4) == 0);
    free(f);
    send_s("peek hello 2 4\n");
    f = rframe(NULL);
    CHECK_S(f, "err hello: 0x2+0x4 is outside its 0x1000-byte view (offsets 4-aligned)\n");
    free(f);
    send_s("peek nope 0 4\n");
    f = rframe(NULL);
    CHECK_S(f, "err no program \"nope\"\n");
    free(f);
    send_s("view pipes\n");
    f = rframe(NULL);
    CHECK_S(f, "pipes system=0 inboxes=0\n");
    free(f);
    send_s("view fleet json\n");
    f = rframe(NULL);
    CHECK_S(f, "{\"fleet\":[{\"system\":0,\"n\":1,\"role\":\"single\",\"text\":\"not in a fleet (single meshd: --cluster / --peer)\"}]}\n");
    free(f);
    send_s("view bogus\n");
    f = rframe(NULL);
    CHECK_S(f, "err view \"bogus\": fleet, jobs or pipes [json]\n");
    free(f);
    send_s("pipes\n");
    f = rframe(&n);
    CHECK_U(n, 0);
    free(f);
    send_s("cluster\n");
    f = rframe(NULL);
    CHECK_S(f, "cluster 0 system 0 state single leader 0 up 0x0 linked 0x0 lock_wait_us 0.0\n");
    free(f);
    send_s("catalog\n");
    f = rframe(NULL);
    CHECK(strncmp(f, "err --programs /nonexistent-catalog: ", 37) == 0);
    free(f);
    send_s("snap\n");
    f = rframe(&n);
    CHECK(n > MESH_SNAP_HOST && (uint8_t)f[MESH_SN_COUNT] == 1);
    free(f);
    send_s("trace\n");
    f = rframe(&n);
    CHECK_U(n, 8 + MESH_TRACE_BYTES * MESH_TRACE_PER_PROG);
    free(f);
    send_s("render\n");
    f = rframe(NULL);
    CHECK_S(f, "err the blob thread is gone\n");
    free(f);
    send_s("infer\n");
    f = rframe(NULL);
    CHECK_S(f, "err no prompt (" MESH_INFER_USAGE ")\n");
    free(f);
    send_s("log 1\n");
    f = rframe(NULL);
    CHECK(strstr(f, "control: unknown or malformed request \"frobnicate\"") != NULL);
    free(f);
    /* The control counters moved. */
    CHECK(atomic_load(&SH->host.net.ctl[2]) >= 20);
    /* A line over 4096 bytes: refused, then closed. */
    char *big = malloc(5000);
    memset(big, 'x', 4999);
    big[4999] = 0;
    send_s(big);
    free(big);
    CHECK_S(rline(), "err request line over 4096 bytes\n");
    char b1;
    CHECK(read(FD, &b1, 1) == 0);
    close(FD);

    /* control.rs an_idle_client_is_closed: a request is answered, then silence closes the connection. */
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    FD = sv[0];
    send_s("log 1\n");
    mesh_control_client(SH, sv[1], 300);
    f = rframe(NULL);
    free(f);
    CHECK(read(sv[0], &b1, 1) == 0);
    close(sv[0]);
    close(lfd);
    mesh_chan_drop_sender(SH->cmd); /* the driver stand-in's recv now ends */
    pthread_join(dt, NULL);
    free(mb.base);
    return mesh_check_done();
}
