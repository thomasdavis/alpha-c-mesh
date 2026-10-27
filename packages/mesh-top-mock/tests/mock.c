/* crates/mesh-top tests/snap.rs mock_snapshot_round_trips, plus the control port spoken raw
 * (the client's own tests drive it through the endpoint thread). */
#include "mesh/check.h"
#include "mesh/top_mock.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void round_trips(void) {
    mt_mock_world w;
    mt_mock_world_init(&w, "m", MESH_PEER_NONE);
    mt_snapshot s = mt_mock_synth(&w);
    mt_snapshot again;
    char *err = NULL;
    CHECK(mt_snapshot_parse(s.bytes, s.len, &again, &err));
    const char *want[5] = {"doom", "train", "infer", "http", "hello"};
    CHECK_U(again.nsections, 5);
    for (size_t i = 0; i < 5 && i < again.nsections; i++) {
        char name[MESH_NAME_BYTES + 1];
        mt_section_name(&again.sections[i], name);
        CHECK_S(name, want[i]);
    }
    CHECK_U(mt_section_view_len(mt_snapshot_by_name(&again, "doom")), MESH_DOOM_SNAP_BYTES);
    CHECK(memcmp(mt_section_view(mt_snapshot_by_name(&again, "hello")), "HELO", 4) == 0);
    mt_snapshot_free(&again);
    mt_snapshot_free(&s);
    mt_mock_world_free(&w);
}

static int dial(const char *addr) {
    const char *colon = strrchr(addr, ':');
    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons((uint16_t)atoi(colon + 1))};
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool read_n(int fd, uint8_t *b, size_t n) {
    while (n) {
        ssize_t k = recv(fd, b, n, 0);
        if (k <= 0) return false;
        b += k;
        n -= (size_t)k;
    }
    return true;
}

static uint8_t *framed(int fd, const char *req, size_t *len) {
    send(fd, req, strlen(req), 0);
    uint8_t h[4];
    if (!read_n(fd, h, 4)) return NULL;
    *len = (size_t)h[0] | (size_t)h[1] << 8 | (size_t)h[2] << 16 | (size_t)h[3] << 24;
    uint8_t *b = malloc(*len + 1);
    if (!read_n(fd, b, *len)) {
        free(b);
        return NULL;
    }
    b[*len] = 0;
    return b;
}

static void line(int fd, const char *req, char *out, size_t cap) {
    send(fd, req, strlen(req), 0);
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
        if (recv(fd, &c, 1, 0) != 1 || c == '\n') break;
        out[n++] = c;
    }
    out[n] = 0;
}

static void server(void) {
    mt_mock *m = mt_mock_start("127.0.0.1:0", "mock-a", 0);
    CHECK(m != NULL);
    if (!m) return;
    int fd = dial(mt_mock_addr(m));
    CHECK(fd >= 0);
    size_t len;
    uint8_t *b = framed(fd, "snap\n", &len);
    CHECK(b != NULL);
    mt_snapshot s;
    char *err = NULL;
    CHECK(b && mt_snapshot_parse(b, len, &s, &err));
    if (b && !err) {
        char host[97];
        mt_snapshot_hostname(&s, host);
        CHECK_S(host, "mock-a");
        CHECK_U(s.nsections, 5);
        mt_snapshot_free(&s);
    }
    free(b);
    char t[128];
    line(fd, "stop train\n", t, sizeof t);
    CHECK_S(t, "ok");
    b = framed(fd, "http /hi\n", &len);
    CHECK(b && strncmp((char *)b, "HTTP/1.0 200 OK", 15) == 0);
    free(b);
    line(fd, "bogus\n", t, sizeof t);
    CHECK(strncmp(t, "err", 3) == 0);
    b = framed(fd, "catalog\n", &len);
    CHECK(b != NULL);
    if (b) {
        char want[256] = "";
        for (int i = 0; i < MT_MOCK_CATALOG_LEN; i++) strcat(strcat(want, MT_MOCK_CATALOG[i]), "\n");
        CHECK_S((char *)b, want);
    }
    free(b);
    b = framed(fd, "programs\n", &len);
    CHECK(b && strstr((char *)b, "\n4 hello running 2 ") && strstr((char *)b, "fields hello magic last_arg last_result"));
    free(b);
    mt_mock_lock(m);
    CHECK_U(mt_mock_prog_named(mt_mock_world_of(m), "train")->state, MESH_ST_LOADED);
    mt_mock_unlock(m);
    CHECK(mt_mock_saw(m, "http /hi"));
    CHECK(!mt_mock_saw(m, "http /nope"));
    mt_mock_stop(m);
    char c;
    CHECK(recv(fd, &c, 1, 0) <= 0); /* the server closed us */
    close(fd);
}

int main(void) {
    mt_clock_virtual(true, 1000000000000u);
    round_trips();
    server();
    return mesh_check_done();
}
