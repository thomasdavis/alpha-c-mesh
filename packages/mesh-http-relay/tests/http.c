/* http.rs's view_routes, and the port over TCP: the views (text, JSON, a bad format), a request relayed to
 * the http program (a stand-in driver), the 503 when it cannot answer, render without a blob thread. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/abi.h"
#include "mesh/check.h"
#include "mesh/http_relay.h"
#include "mesh/log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static mesh_shared *SH;
static int answer = 1;

static void *driver(void *arg) {
    (void)arg;
    mesh_cmd c;
    while (mesh_chan_recv(SH->cmd, &c, -1) == MESH_CHAN_OK) {
        if (c.k == MESH_CMD_REQ && answer) mesh_reply_bytes(c.reply, "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nhi", 40);
        else mesh_reply_errf(c.reply, "no program \"%s\" (`programs` lists them)", c.name);
        c.reply = NULL;
        mesh_cmd_free(&c);
    }
    return NULL;
}

static int lfd, port;
static void *serve(void *arg) {
    (void)arg;
    mesh_http_serve(SH, lfd);
    return NULL;
}

/* One request; the whole response. */
static char *get(const char *head) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(fd, (struct sockaddr *)&sa, sizeof sa);
    if (write(fd, head, strlen(head)) < 0) abort();
    mesh_buf b = MESH_BUF_INIT;
    char buf[4096];
    ssize_t k;
    while ((k = read(fd, buf, sizeof buf)) > 0) mesh_buf_put(&b, buf, (size_t)k);
    close(fd);
    return mesh_buf_take(&b);
}

int main(void) {
    const char *v, *q;
    size_t ql;
    CHECK(mesh_http_view_route("GET /fleet HTTP/1.0", &v, &q, &ql) && strcmp(v, "fleet") == 0 && ql == 0);
    CHECK(mesh_http_view_route("GET /jobs?format=json HTTP/1.1", &v, &q, &ql) && strcmp(v, "jobs") == 0 && ql == 11 && strncmp(q, "format=json", 11) == 0);
    CHECK(mesh_http_view_route("GET /pipes", &v, &q, &ql) && strcmp(v, "pipes") == 0);
    CHECK(!mesh_http_view_route("GET /fleetx HTTP/1.0", &v, &q, &ql));
    CHECK(!mesh_http_view_route("POST /fleet HTTP/1.0", &v, &q, &ql));
    CHECK(!mesh_http_view_route("GET /status HTTP/1.0", &v, &q, &ql));
    const char *w;
    char *job;
    CHECK(mesh_http_render_route("GET /render?job=4.1 HTTP/1.0", &w, &job) && strcmp(w, "status") == 0 && strcmp(job, "4.1") == 0);
    free(job);
    CHECK(mesh_http_render_route("GET /render.bmp?x=1&job= HTTP/1.1", &w, &job) && strcmp(w, "bmp") == 0 && strcmp(job, "latest") == 0);
    free(job);
    CHECK(!mesh_http_render_route("GET /render.ppm HTTP/1.0", &w, &job));
    CHECK(!mesh_http_render_route("GET /render HTTP/1.0", &w, &job));

    mesh_log_set_fd(-1);
    static _Atomic bool quit;
    mesh_mailbox mb = mesh_mailbox_test();
    SH = mesh_shared_new(mb, "/tmp", false, 0, 1, &quit);
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
    port = ntohs(sa.sin_port);
    pthread_create(&st, NULL, serve, NULL);

    char *r = get("GET /fleet HTTP/1.0\r\n\r\n");
    const char *body = "fleet system=0 n=1 role=single -- not in a fleet (single meshd: --cluster / --peer)\n";
    char want[512];
    snprintf(want, sizeof want, "HTTP/1.0 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n\r\n%s", strlen(body), body);
    CHECK_S(r, want);
    free(r);
    r = get("GET /pipes?format=json HTTP/1.0\r\n\r\n");
    CHECK(strstr(r, "Content-Type: application/json\r\n") && strstr(r, "\r\n\r\n{\"pipes\":[{\"system\":0,\"inboxes\":0}]}\n"));
    free(r);
    r = get("GET /jobs?format=xml HTTP/1.0\r\n\r\n");
    CHECK(strncmp(r, "HTTP/1.0 400 Bad Request\r\n", 26) == 0 && strstr(r, "\r\n\r\nmeshd: format=xml: json or text\n"));
    free(r);
    r = get("GET /status HTTP/1.0\r\n\r\n");
    CHECK_S(r, "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nhi");
    free(r);
    CHECK_U(mesh_host_block_get_u32(&SH->host, MESH_S_HTTP_REQUESTS), 1);
    answer = 0;
    r = get("GET /status HTTP/1.0\r\n\r\n");
    body = "meshd: no program \"http\" (`programs` lists them)\n";
    snprintf(want, sizeof want, "HTTP/1.0 503 Service Unavailable\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
    CHECK_S(r, want);
    free(r);
    r = get("GET /render?job=latest HTTP/1.0\r\n\r\n");
    CHECK(strncmp(r, "HTTP/1.0 404 Not Found\r\n", 24) == 0 && strstr(r, "meshd: the blob thread is gone\n"));
    free(r);
    r = get("GET /infer?max=4 HTTP/1.0\r\n\r\n");
    CHECK(strncmp(r, "HTTP/1.0 400 Bad Request\r\n", 26) == 0 && strstr(r, "meshd: infer: no prompt= parameter\n"));
    free(r);
    CHECK(atomic_load(&SH->host.net.http[2]) == 7);
    mesh_chan_drop_sender(SH->cmd);
    pthread_join(dt, NULL);
    free(mb.base);
    return mesh_check_done();
}
