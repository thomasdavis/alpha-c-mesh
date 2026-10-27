/* Asking the driver (answered, dropped, timed out), the http relay's counting, link queues, fail-stop. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/check.h"
#include "mesh/log.h"
#include "mesh/shared.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static void *driver(void *arg) {
    mesh_shared *sh = arg;
    mesh_cmd c;
    while (mesh_chan_recv(sh->cmd, &c, 2000) == MESH_CHAN_OK) {
        if (c.k == MESH_CMD_ROLLBACK) mesh_reply_errf(c.reply, "no program \"%s\" (`programs` lists them)", c.name);
        else if (c.k == MESH_CMD_REQ) mesh_reply_bytes(c.reply, "HTTP/1.0 200 OK\r\n\r\nhi", 21);
        else if (c.k == MESH_CMD_RESET) { mesh_reply_drop(c.reply); }
        else if (c.k == MESH_CMD_UNLOAD) { mesh_cmd_free(&c); return NULL; }
        else mesh_reply_ok(c.reply, "blink 412");
        c.reply = NULL;
        mesh_cmd_free(&c);
    }
    return NULL;
}

int main(void) {
    static _Atomic bool quit;
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_shared *sh = mesh_shared_new(mb, "out/programs", false, 0, 1, &quit);
    pthread_t th;
    pthread_create(&th, NULL, driver, sh);
    char *t;
    mesh_cmd c = mesh_cmd_new(MESH_CMD_LOAD);
    c.has_src = true;
    c.src = mesh_src_catalog("hello");
    CHECK(mesh_shared_ask(sh, c, 1000, &t));
    CHECK_S(t, "blink 412");
    free(t);
    c = mesh_cmd_new(MESH_CMD_ROLLBACK);
    c.name = mesh_xstrdup("x");
    CHECK(!mesh_shared_ask(sh, c, 1000, &t));
    CHECK_S(t, "no program \"x\" (`programs` lists them)");
    free(t);
    /* Dropped unanswered: at once, with the Rust's words. */
    c = mesh_cmd_new(MESH_CMD_RESET);
    int64_t t0 = mesh_mono_ms();
    CHECK(!mesh_shared_ask(sh, c, 60000, &t));
    CHECK_S(t, "no answer from the driver in 60s");
    CHECK(mesh_mono_ms() - t0 < 1000);
    free(t);
    uint8_t *resp;
    size_t rn;
    char *err = NULL;
    CHECK(mesh_shared_http(sh, (const uint8_t *)"GET /status HTTP/1.0\r\n\r\n", 24, &resp, &rn, &err));
    CHECK(rn == 21 && memcmp(resp, "HTTP/1.0 200 OK", 15) == 0);
    free(resp);
    CHECK_U(mesh_host_block_get_u32(&sh->host, MESH_S_HTTP_REQUESTS), 1);
    uint8_t snap[MESH_SNAP_HOST];
    mesh_host_block_bytes(&sh->host, snap);
    CHECK(memcmp(snap + MESH_S_LAST_HTTP, "GET /status HTTP/1.0\0", 21) == 0);
    c = mesh_cmd_new(MESH_CMD_UNLOAD);
    mesh_chan_send(sh->cmd, &c);
    pthread_join(th, NULL);
    /* Nobody listens: a timeout. */
    c = mesh_cmd_new(MESH_CMD_PROG_RUN);
    CHECK(!mesh_shared_ask(sh, c, 30, &t));
    CHECK_S(t, "no answer from the driver in 30ms");
    free(t);
    /* Not in a fleet. */
    CHECK(!mesh_shared_change(sh, "load x", 100, &t));
    CHECK_S(t, "not in a fleet");
    free(t);

    /* Link queues: copies, bounded, broadcast to the linked ones only. */
    const uint8_t f[3] = {MESH_F_BEAT, 1, 2};
    CHECK(!mesh_shared_send_peer(sh, 1, f, 3));
    sh->link_out[1] = mesh_link_queue_new(2);
    sh->link_out[3] = mesh_link_queue_new(2);
    CHECK(mesh_shared_send_peer(sh, 1, f, 3));
    CHECK(mesh_shared_broadcast(sh, f, 3));
    CHECK(!mesh_shared_send_peer(sh, 1, f, 3)); /* full */
    CHECK(!mesh_shared_send_peer_wait(sh, 1, f, 3, 20));
    mesh_link_frame x;
    CHECK(mesh_chan_try_recv(sh->link_out[1], &x) == MESH_CHAN_OK && x.n == 3 && x.b[0] == MESH_F_BEAT);
    free(x.b);
    CHECK(mesh_shared_send_peer_wait(sh, 1, f, 3, 20));
    CHECK_U(mesh_chan_len(sh->link_out[3]), 1);
    mesh_log_set_fd(-1);
    mesh_shared_fail_stop(sh, "disk full");
    CHECK(atomic_load(&quit) && !mesh_shared_send_peer(sh, 3, f, 3));
    mesh_shared_free(sh);
    free(mb.base);
    return mesh_check_done();
}
