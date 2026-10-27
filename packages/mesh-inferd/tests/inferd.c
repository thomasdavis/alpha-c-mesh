/* A gpt2 worker on loopback answers the request meshd makes (single meshd: mono=0), its K_ERROR, and no worker. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/check.h"
#include "mesh/inferd.h"
#include "mesh/log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int lfd;
static void *worker(void *arg) {
    int fail = *(int *)arg;
    int c = accept(lfd, NULL, NULL);
    uint8_t k;
    mesh_buf b = MESH_BUF_INIT;
    char *err = NULL;
    if (mesh_infer_read_frame(c, &k, &b, NULL, &err) == 0) {
        mesh_infer_msg q, a;
        mesh_infer_msg_decode(b.data, b.len, &q, &err);
        mesh_infer_msg_init(&a);
        mesh_infer_msg_set(&a, "gen_tokens", mesh_infer_msg_get(&q, "max"));
        mesh_infer_msg_set(&a, "mode", mesh_infer_msg_get(&q, "mode"));
        mesh_infer_msg_set_body(&a, fail ? "  out of memory \n" : q.body);
        mesh_buf e = MESH_BUF_INIT;
        mesh_infer_msg_encode(&a, &e);
        mesh_infer_write_frame(c, fail ? MESH_INFER_K_ERROR : MESH_INFER_K_RESULT, e.data, e.len);
        mesh_buf_free(&e);
        mesh_infer_msg_free(&q);
        mesh_infer_msg_free(&a);
    }
    mesh_buf_free(&b);
    close(c);
    return NULL;
}

int main(void) {
    mesh_log_set_fd(-1);
    /* A catalog with gpt2 at param 0: its worker listens on the control port's base. */
    char root[64], p[128];
    snprintf(root, sizeof root, "/tmp/mesh-inferd-%d", (int)getpid());
    mkdir(root, 0700);
    snprintf(p, sizeof p, "%s/gpt2", root);
    mkdir(p, 0700);
    snprintf(p, sizeof p, "%s/gpt2/program.toml", root);
    FILE *f = fopen(p, "w");
    fputs("name = \"gpt2\"\nabi = 6\nbackends = [\"native\"]\n", f);
    fclose(f);
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(lfd, (struct sockaddr *)&sa, sizeof sa);
    socklen_t sl = sizeof sa;
    getsockname(lfd, (struct sockaddr *)&sa, &sl);
    listen(lfd, 4);
    char ctl[64];
    snprintf(ctl, sizeof ctl, "127.0.0.1:%u", ntohs(sa.sin_port));
    static _Atomic bool quit;
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_shared *sh = mesh_shared_new(mb, root, false, 0, 1, &quit);
    char *err = NULL;
    CHECK_I(mesh_infer_svc_single(&sh->svc, ctl, &err), 0);
    sh->has_svc = true;
    mesh_infer_request r;
    CHECK_I(mesh_infer_parse("exact max 4 mono=0 Hello there", &r, &err), 0);
    int fail = 0;
    pthread_t th;
    pthread_create(&th, NULL, worker, &fail);
    mesh_infer_msg res;
    CHECK_I(mesh_inferd_run(sh, &r, &res, &err), 0);
    pthread_join(th, NULL);
    CHECK_S(res.body, "Hello there");
    CHECK_S(mesh_infer_msg_get(&res, "gen_tokens"), "4");
    CHECK_S(mesh_infer_msg_get(&res, "program"), "gpt2");
    CHECK_S(mesh_infer_msg_get(&res, "coordinator"), "0");
    CHECK_S(mesh_infer_msg_get(&res, "decode_sys"), "0");
    CHECK(mesh_infer_msg_get(&res, "request_ms") != NULL);
    mesh_infer_msg_free(&res);
    fail = 1;
    pthread_create(&th, NULL, worker, &fail);
    CHECK_I(mesh_inferd_run(sh, &r, &res, &err), -1);
    pthread_join(th, NULL);
    CHECK_S(err, "gpt2 on member 0: out of memory");
    free(err);
    close(lfd);
    CHECK_I(mesh_inferd_run(sh, &r, &res, &err), -1);
    CHECK(strstr(err, "gpt2 on member 0 (127.0.0.1:") == err);
    CHECK(strstr(err, "Connection refused (os error 111)") != NULL);
    free(err);
    /* Nothing placed (single, not loaded): no candidates. */
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("hi", &r, &err), 0);
    CHECK_I(mesh_inferd_run(sh, &r, &res, &err), -1);
    CHECK_S(err, "no gpt2, gpt2-prefill or gpt2-decode is placed on any member");
    free(err);
    mesh_infer_request_free(&r);
    mesh_shared_free(sh);
    free(mb.base);
    unlink(p);
    snprintf(p, sizeof p, "%s/gpt2", root);
    rmdir(p);
    rmdir(root);
    return mesh_check_done();
}
