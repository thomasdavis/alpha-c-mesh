/* crates/mesh-top tests/client.rs, ported: the endpoint thread against the mock control server:
 * polling, the v2 commands (programs, catalog, load, upgrade, rollback, unload, input, log,
 * trace), and reconnect after the server goes away and comes back. */
#include "mesh/check.h"
#include "mesh/top_client.h"
#include "mesh/top_mock.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t ms_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

typedef bool (*want_fn)(mt_update *u, void *ctx);

/* Waits for an update matching f (which may keep it: then it sets *kept), failing after secs. */
static bool wait_for(mt_updates *q, unsigned secs, want_fn f, void *ctx) {
    uint64_t end = ms_now() + secs * 1000u;
    for (;;) {
        uint64_t now = ms_now();
        mt_update u;
        if (now >= end || !mt_updates_pop(q, (uint32_t)(end - now), &u)) {
            fprintf(stderr, "timed out waiting for update\n");
            CHECK(false);
            return false;
        }
        bool hit = f(&u, ctx);
        mt_update_free(&u);
        if (hit) return true;
    }
}

typedef struct {
    const char *line;
    bool bytes;
    uint8_t *data;
    size_t len;
} asked;

static bool is_reply(mt_update *u, void *ctx) {
    asked *a = ctx;
    if (u->kind != MT_UPDATE_REPLY || u->machine != 0 || strcmp(u->text, a->line) != 0) return false;
    a->bytes = u->bytes;
    a->data = u->data;
    a->len = u->len;
    u->data = NULL;
    return true;
}

/* Sends line and returns its reply (snapshots in between are skipped). */
static asked ask(mt_endpoint *ep, mt_updates *q, const char *line) {
    asked a = {line, false, NULL, 0};
    mt_endpoint_send(ep, line);
    wait_for(q, 5, is_reply, &a);
    return a;
}

/* The text reply (freed into a static buffer); "<bytes>" when it was framed. */
static const char *text(asked a) {
    static char buf[256];
    if (a.bytes || !a.data) snprintf(buf, sizeof buf, "<%zu bytes>", a.len);
    else snprintf(buf, sizeof buf, "%s", (char *)a.data);
    free(a.data);
    return buf;
}

/* The framed reply, NUL-terminated (caller frees); NULL when it was text. */
static char *bytes(asked a) {
    if (!a.bytes) {
        fprintf(stderr, "expected bytes, got %s\n", a.data ? (char *)a.data : "nothing");
        free(a.data);
        return NULL;
    }
    char *s = malloc(a.len + 1);
    memcpy(s, a.data, a.len);
    s[a.len] = 0;
    free(a.data);
    return s;
}

static bool five_snaps(mt_update *u, void *ctx) {
    int *n = ctx;
    if (u->kind == MT_UPDATE_SNAP && u->machine == 0) {
        char host[97];
        mt_snapshot_hostname(&u->snap, host);
        CHECK_S(host, "mock-a");
        CHECK_U(u->snap.nsections, 5);
        ++*n;
    }
    return *n >= 5;
}

static bool is_offline(mt_update *u, void *ctx) {
    (void)ctx;
    return u->kind == MT_UPDATE_OFFLINE && u->machine == 0;
}

static bool is_online(mt_update *u, void *ctx) {
    (void)ctx;
    return u->kind == MT_UPDATE_ONLINE && u->machine == 0;
}

static bool any_snap(mt_update *u, void *ctx) {
    (void)ctx;
    return u->kind == MT_UPDATE_SNAP && u->machine == 0;
}

static void polls_commands_and_reconnects(void) {
    /* The world outlives both servers (Rust shares it in an Arc). */
    mt_mock_world *w = malloc(sizeof *w);
    mt_mock_world_init(w, "mock-a", 0);
    mt_mock *m = mt_mock_start_with("127.0.0.1:0", w);
    char addr[64];
    snprintf(addr, sizeof addr, "%s", mt_mock_addr(m));
    mt_updates *q = mt_updates_new();
    mt_endpoint *ep = mt_endpoint_spawn(0, addr, q, 20);

    /* Several v2 snapshots arrive at ~50 Hz. */
    int n = 0;
    wait_for(q, 5, five_snaps, &n);

    /* Text commands, the framed http reply, an unknown command. */
    CHECK_S(text(ask(ep, q, "stop train")), "ok");
    char *b = bytes(ask(ep, q, "http /hi"));
    CHECK(b && strncmp(b, "HTTP/1.0 200 OK", 15) == 0);
    free(b);
    CHECK(strncmp(text(ask(ep, q, "bogus")), "err", 3) == 0);
    mt_mock_lock(m);
    CHECK_U(mt_mock_prog_named(mt_mock_world_of(m), "train")->state, MESH_ST_LOADED);
    mt_mock_unlock(m);
    CHECK(mt_mock_saw(m, "http /hi"));

    /* Server goes away: OFFLINE, and commands fail fast instead of queuing. */
    mt_mock_stop(m);
    wait_for(q, 5, is_offline, NULL);
    CHECK_S(text(ask(ep, q, "start train")), "err offline");

    /* Back on the same port with the same world: online again and snapshots resume. */
    mt_mock *m2 = mt_mock_start_with(addr, w);
    CHECK(m2 != NULL);
    wait_for(q, 10, is_online, NULL);
    wait_for(q, 5, any_snap, NULL);
    CHECK_S(text(ask(ep, q, "start train")), "ok");
    mt_mock_stop(m2);
    mt_endpoint_free(ep);
    mt_updates_free(q);
    mt_mock_world_free(w);
    free(w);
}

typedef struct {
    const char *name;
    bool has;
    mt_snapshot snap;
} snap_with;

static bool snap_has(mt_update *u, void *ctx) {
    snap_with *s = ctx;
    if (u->kind != MT_UPDATE_SNAP || u->machine != 0) return false;
    if ((mt_snapshot_by_name(&u->snap, s->name) != NULL) != s->has) return false;
    s->snap = u->snap;
    memset(&u->snap, 0, sizeof u->snap);
    return true;
}

static void program_lifecycle_against_the_mock(void) {
    mt_mock *m = mt_mock_start("127.0.0.1:0", "mock-b", MESH_PEER_NONE);
    mt_updates *q = mt_updates_new();
    mt_endpoint *ep = mt_endpoint_spawn(0, mt_mock_addr(m), q, 50);

    /* Framed text replies: catalog, programs, log. */
    char *cat = bytes(ask(ep, q, "catalog"));
    char want[256] = "";
    for (int i = 0; i < MT_MOCK_CATALOG_LEN; i++) strcat(strcat(want, MT_MOCK_CATALOG[i]), "\n");
    CHECK(cat != NULL);
    if (cat) CHECK_S(cat, want);
    free(cat);
    char *progs = bytes(ask(ep, q, "programs"));
    CHECK(progs && (strncmp(progs, "4 hello running 2 ", 18) == 0 || strstr(progs, "\n4 hello running 2 ")));
    CHECK(progs && strstr(progs, "fields hello magic last_arg last_result"));
    free(progs);

    /* load, then a refused duplicate and an unknown dir. */
    CHECK_S(text(ask(ep, q, "load out/programs/life")), "ok slot 5 blink 1840");
    CHECK_S(text(ask(ep, q, "load life/program.toml")), "err name exists");
    CHECK(strncmp(text(ask(ep, q, "load nope")), "err manifest", 12) == 0);
    /* upgrade keeps the old image for rollback; rollback swaps back. */
    mt_mock_lock(m);
    uint64_t h0 = mt_mock_prog_named(mt_mock_world_of(m), "life")->hash;
    mt_mock_unlock(m);
    CHECK_S(text(ask(ep, q, "upgrade life")), "ok blink 2100 view kept");
    mt_mock_lock(m);
    CHECK(mt_mock_prog_named(mt_mock_world_of(m), "life")->hash != h0);
    mt_mock_unlock(m);
    CHECK_S(text(ask(ep, q, "rollback life")), "ok blink 900");
    mt_mock_lock(m);
    CHECK_U(mt_mock_prog_named(mt_mock_world_of(m), "life")->hash, h0);
    mt_mock_unlock(m);
    CHECK(strncmp(text(ask(ep, q, "rollback hello")), "err", 3) == 0);
    /* The next snapshot has the new section, with its fields. */
    snap_with sw = {"life", true, {0}};
    if (wait_for(q, 5, snap_has, &sw)) {
        const mt_section *life = mt_snapshot_by_name(&sw.snap, "life");
        CHECK_U(mt_section_slot(life), 5);
        CHECK_U(mt_section_pt(life, MESH_PT_LOADS), 3);
        mt_field fs[MESH_MAX_FIELDS];
        size_t nf = mt_section_fields(life, fs);
        bool density = false;
        for (size_t i = 0; i < nf; i++) density |= strcmp(fs[i].name, "density") == 0;
        CHECK(density);
        mt_snapshot_free(&sw.snap);
    }

    /* An on_demand task, doom input (and the v1 `key` alias), reset. */
    CHECK_S(text(ask(ep, q, "task hello 0 5")), "ok 22");
    mt_mock_lock(m);
    float pos0[2] = {mt_mock_world_of(m)->pos[0], mt_mock_world_of(m)->pos[1]};
    mt_mock_unlock(m);
    CHECK_S(text(ask(ep, q, "input doom w")), "ok");
    CHECK_S(text(ask(ep, q, "key w")), "ok");
    mt_mock_lock(m);
    CHECK(memcmp(mt_mock_world_of(m)->pos, pos0, sizeof pos0) != 0); /* two steps forward */
    mt_mock_unlock(m);
    CHECK_S(text(ask(ep, q, "reset train")), "ok");
    CHECK_S(text(ask(ep, q, "train-reset")), "ok");
    char *helo = bytes(ask(ep, q, "peek hello 0 4"));
    CHECK(helo != NULL);
    if (helo) CHECK_S(helo, "HELO");
    free(helo);

    /* unload: gone from the next snapshot; a second unload is refused. */
    CHECK_S(text(ask(ep, q, "unload life")), "ok");
    CHECK(strncmp(text(ask(ep, q, "unload life")), "err", 3) == 0);
    sw = (snap_with){"life", false, {0}};
    if (wait_for(q, 5, snap_has, &sw)) mt_snapshot_free(&sw.snap);

    /* trace parses; log has the lifecycle lines. */
    asked tr = ask(ep, q, "trace");
    CHECK(tr.bytes);
    mt_trace_prog *t = NULL;
    size_t nt = 0;
    char *err = NULL;
    CHECK(mt_parse_trace(tr.data, tr.len, &t, &nt, &err));
    free(tr.data);
    CHECK_U(nt, 5);
    for (size_t i = 0; i < nt; i++) CHECK_U(t[i].nrecs, t[i].n < MESH_TRACE_PER_PROG ? t[i].n : MESH_TRACE_PER_PROG);
    mt_trace_free(t, nt);
    free(err);
    char *log = bytes(ask(ep, q, "log 50"));
    const char *wants[] = {"load out/programs/life", "upgrade life", "rollback life", "unload life", "ERROR load nope"};
    for (size_t i = 0; i < 5; i++) {
        bool has = log && strstr(log, wants[i]);
        if (!has) fprintf(stderr, "log missing %s\n%s\n", wants[i], log ? log : "");
        CHECK(has);
    }
    free(log);
    mt_endpoint_free(ep);
    mt_mock_stop(m);
    mt_updates_free(q);
}

static bool count_offline(mt_update *u, void *ctx) {
    int *n = ctx;
    if (u->kind == MT_UPDATE_OFFLINE && u->machine == 0) ++*n;
    return *n >= 3;
}

static void unreachable_endpoint_reports_offline(void) {
    /* Bind then drop to get a port that is (almost certainly) closed. */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t sl = sizeof sa;
    bind(fd, (struct sockaddr *)&sa, sizeof sa);
    getsockname(fd, (struct sockaddr *)&sa, &sl);
    close(fd);
    char addr[64];
    snprintf(addr, sizeof addr, "127.0.0.1:%u", (unsigned)ntohs(sa.sin_port));
    mt_updates *q = mt_updates_new();
    mt_endpoint *ep = mt_endpoint_spawn(0, addr, q, 20);
    /* Backoff: several attempts within a few seconds. */
    int offline = 0;
    wait_for(q, 5, count_offline, &offline);
    mt_endpoint_free(ep);
    mt_updates_free(q);
}

int main(void) {
    polls_commands_and_reconnects();
    program_lifecycle_against_the_mock();
    unreachable_endpoint_reports_offline();
    CHECK_U(mt_reply_timeout_ms("load x"), 20000);
    CHECK_U(mt_reply_timeout_ms("train-reset"), 15000);
    CHECK_U(mt_reply_timeout_ms(" task hello 0 5"), 8000);
    CHECK_U(mt_reply_timeout_ms("snap"), 3000);
    CHECK(mt_binary_reply(" load "));
    CHECK(!mt_binary_reply("load life"));
    CHECK(mt_binary_reply("trace"));
    CHECK(!mt_binary_reply("stop train"));
    return mesh_check_done();
}
