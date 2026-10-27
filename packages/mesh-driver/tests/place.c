/* Placement in the driver (driver.rs's placement section) over a fake CPU executor: a peered driver,
 * member 0 of 2, with a placeable program. Its placer exists from the load; `place` reports and asks for
 * a change (as the leader: pin here, done at once with the other member down; as a follower: refused,
 * the leader coordinates); the leader's PL_SET pins it on the other member (its state written into the
 * view, the kernel stopped here, start and reset refused with the Rust's reason, input forwarded with
 * F_INPUT, PL_DONE answered, the inventory still counting it run) and back to local (it runs again);
 * the fleet's view reaches the placer; unload drops it. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/check.h"
#include "mesh/driver.h"
#include "mesh/log.h"
#include "mesh/manifest.h"
#include "mesh/place_wire.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- a fake executor ---- */

static mesh_mailbox MB;

typedef struct {
    pthread_t th;
    bool started;
    _Atomic bool running;
    uint32_t slot, nodes;
} fake_kernel;

static bool k_zero(const mesh_kernel *k, uint32_t *o, char **e) { (void)k, (void)e; *o = 0; return true; }
static bool k_one(const mesh_kernel *k, uint32_t *o, char **e) { (void)k, (void)e; *o = 1; return true; }
static uint64_t k_scratch(const mesh_kernel *k) { (void)k; return 0; }
static bool k_global(const mesh_kernel *k, const char *n, uint32_t *o, char **e) {
    (void)k;
    if (strcmp(n, MESH_RT_ABI_SYMBOL)) { *e = strdup("no such global"); return false; }
    *o = MESH_RT_ABI;
    return true;
}
static void k_drop(mesh_kernel *k) {
    fake_kernel *f = k->self;
    if (f->started) pthread_join(f->th, NULL);
    free(f);
}
static const mesh_kernel_ops KOPS = {MESH_BACKEND_WASM, k_zero, k_zero, k_one, k_scratch, k_global, k_drop};

/* The node loop: online, take tickets in order, run them (OP_INIT writes a magic; a task is arg * 3 + 7), halt on P_SHUTDOWN. */
static void *nodes(void *arg) {
    fake_kernel *f = arg;
    size_t c = mesh_prog_ctl(f->slot);
    mesh_mailbox_fetch_add(MB, c + MESH_P_ONLINE, f->nodes);
    uint32_t view = mesh_mailbox_get(MB, mesh_prog_entry(f->slot) + MESH_PT_VIEW_OFF);
    for (;;) {
        if (mesh_mailbox_get(MB, c + MESH_P_SHUTDOWN)) break;
        uint32_t t = mesh_mailbox_get(MB, c + MESH_P_RING_HEAD);
        size_t e = c + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING);
        if (mesh_mailbox_get(MB, e + MESH_E_SEQ) != t + 1) { mesh_sleep_us(50); continue; }
        uint32_t op = mesh_mailbox_get(MB, e + MESH_E_OP), a = mesh_mailbox_get(MB, e + MESH_E_ARG), k = mesh_mailbox_get(MB, e + MESH_E_SLOT);
        uint32_t r = 0;
        if (op == MESH_OP_INIT) mesh_mailbox_set(MB, view, 0x4f4c4548u);
        else if (op == MESH_OP_TASK) r = a * 3 + 7;
        else r = MESH_R_BAD_OP;
        mesh_mailbox_set(MB, c + MESH_P_RING_HEAD, t + 1);
        mesh_mailbox_set(MB, c + MESH_P_RESULTS + 4 * k, r);
        mesh_mailbox_publish(MB, c + MESH_P_MARKS + 4 * k, t + 1);
        mesh_mailbox_fetch_add(MB, c + MESH_P_DONE, 1);
    }
    mesh_mailbox_fetch_add(MB, c + MESH_P_HALTED, f->nodes);
    atomic_store(&f->running, false);
    return NULL;
}

static mesh_caps CAPS = {MESH_BACKEND_WASM, "fake", 0, 0, 1, 1, false, 0, 0, 0, 0, "fake"};
static const mesh_caps *b_caps(const mesh_backend *b) { (void)b; return &CAPS; }
static bool b_true(const mesh_backend *b) { (void)b; return true; }
static bool b_false(const mesh_backend *b) { (void)b; return false; }
static uint32_t b_iso(const mesh_backend *b) { (void)b; return MESH_ISOLATION_LEASE; }
static bool b_mailbox(const mesh_backend *b, uint8_t **h, uint64_t *dev, char **e) { (void)b, (void)e; *h = MB.base; *dev = 0; return true; }
static bool b_current(const mesh_backend *b, char **e) { (void)b, (void)e; return true; }

static bool c_needs_ptx(const mesh_compiler *c) { (void)c; return false; }
static bool c_prepare(const mesh_compiler *c, mesh_payload *src, const uint64_t *expect, const char *root, mesh_prepared *out, char **err) {
    (void)c, (void)expect, (void)root;
    memset(out, 0, sizeof *out);
    if (!mesh_check_manifest(src, MESH_BACKEND_WASM, &out->manifest, err)) { mesh_payload_free(src); return false; }
    out->hash = 0x1234;
    out->code_len = strlen(src->manifest);
    out->code = (uint8_t *)strdup(src->manifest);
    out->src = *src;
    memset(src, 0, sizeof *src);
    out->exec = MESH_BACKEND_WASM;
    out->kernel = calloc(1, sizeof *out->kernel);
    out->kernel->ops = &KOPS;
    out->kernel->self = calloc(1, sizeof(fake_kernel));
    return true;
}
static const mesh_compiler_ops COPS = {c_needs_ptx, c_prepare};
static const mesh_compiler COMP = {&COPS, NULL};
static const mesh_compiler *b_compiler(const mesh_backend *b) { (void)b; return &COMP; }

static bool b_load(const mesh_backend *b, mesh_prepared *p, mesh_kernel **out, char **e) {
    (void)b, (void)e;
    *out = p->kernel;
    p->kernel = NULL;
    return true;
}
static bool b_stream(const mesh_backend *b, uint32_t slot, const mesh_kernel *k, mesh_stream **out, char **e) {
    (void)b, (void)k, (void)e;
    *out = calloc(1, sizeof **out);
    (*out)->exec = MESH_BACKEND_WASM;
    (*out)->slot = slot;
    return true;
}
static bool b_launch(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, uint32_t n, uint64_t dev, uint32_t slot, char **e) {
    (void)b, (void)st, (void)dev, (void)e;
    fake_kernel *f = k->self;
    if (f->started) pthread_join(f->th, NULL);
    f->slot = slot;
    f->nodes = n;
    atomic_store(&f->running, true);
    f->started = true;
    pthread_create(&f->th, NULL, nodes, f);
    return true;
}
static bool b_query(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, bool *done, char **e) {
    (void)b, (void)st, (void)e;
    *done = !atomic_load(&((fake_kernel *)k->self)->running);
    return true;
}
static bool b_unload(const mesh_backend *b, mesh_kernel *k, char **e) {
    (void)b, (void)e;
    mesh_kernel_drop(k);
    return true;
}
static bool b_stack(const mesh_backend *b, size_t *o, char **e) { (void)b, (void)e; *o = 0; return true; }
static bool b_set_stack(const mesh_backend *b, size_t n, char **e) { (void)b, (void)n, (void)e; return true; }
static bool b_scope(const mesh_backend *b, bool has, uint32_t slot, uint32_t *scope) { (void)b; *scope = has ? slot : MESH_NO_SLOT; return true; }

static void b_destroy(mesh_backend *b) { free(b); }
static const mesh_backend_ops BOPS = {b_caps, b_true, b_false, b_iso, b_mailbox, b_current, b_compiler, b_load, b_stream, b_launch, b_query,
                                       b_unload, b_stack, b_set_stack, b_scope, NULL, NULL, NULL, NULL, NULL, b_destroy};

/* ---- helpers ---- */

static mesh_shared *SH;

static bool ask(mesh_cmd_kind k, const char *name, bool on, char **text) {
    mesh_cmd c = mesh_cmd_new(k);
    if (name) c.name = strdup(name);
    c.on = on;
    return mesh_shared_ask(SH, c, 10000, text);
}

static bool load(const char *dir, char **text) {
    mesh_cmd c = mesh_cmd_new(MESH_CMD_LOAD);
    c.has_src = true;
    c.src = mesh_src_catalog(dir);
    return mesh_shared_ask(SH, c, 10000, text);
}


static bool place(const char *name, const char *words, char **text) {
    mesh_cmd c = mesh_cmd_new(MESH_CMD_PLACE);
    c.name = strdup(name);
    if (words) {
        c.has_words = true;
        c.words = calloc(8, sizeof *c.words);
        char *w = strdup(words), *save = NULL;
        for (char *t = strtok_r(w, " ", &save); t; t = strtok_r(NULL, " ", &save)) c.words[c.nwords++] = strdup(t);
        free(w);
    }
    return mesh_shared_ask(SH, c, 10000, text);
}

static void fleet_view(uint32_t up, bool has_leader, uint32_t leader) {
    mesh_cmd c = mesh_cmd_new(MESH_CMD_FLEET_VIEW);
    c.up = up;
    c.has_leader = has_leader;
    c.leader = leader;
    mesh_chan_send(SH->cmd, &c);
}

static void place_free(void *m) {
    mpw_place_free(m);
    free(m);
}

/* F_PLACE from the leader (system 1): PL_SET mode, the members, the state from system `src`. */
static void pl_set(mpm_mode mode, uint32_t id, uint32_t members, uint32_t src, size_t state_len) {
    mpw_place *m = calloc(1, sizeof *m);
    m->op = MESH_PL_SET;
    m->id = id;
    strcpy(m->name, "dm");
    m->mode = mode;
    m->aux = members | src << 8;
    m->nbytes = state_len;
    m->bytes = calloc(1, state_len ? state_len : 1);
    for (size_t i = 0; i < state_len; i++) m->bytes[i] = (uint8_t)(i * 7 + 1);
    mesh_cmd c = mesh_cmd_new(MESH_CMD_PEER_PLACE);
    c.msg = m;
    c.msg_free = place_free;
    c.from = 1;
    mesh_chan_send(SH->cmd, &c);
}

/* The next frame queued for system 1 (type byte), or -1 within 2 s. */
static int next_frame(mesh_link_frame *out) {
    for (int i = 0; i < 2000; i++) {
        if (mesh_chan_try_recv(SH->link_out[1], out) == MESH_CHAN_OK) {
            if (out->n == 0) continue;
            return out->b[0];
        }
        mesh_sleep_us(1000);
    }
    return -1;
}

static void *run(void *arg) {
    *(int *)((void **)arg)[0] = mesh_driver_run((mesh_driver *)((void **)arg)[1]);
    return NULL;
}

static uint32_t p_run(void) { return mesh_mailbox_get(MB, mesh_prog_ctl(0) + MESH_P_RUN); }

static bool wait_state(uint32_t st) {
    for (int i = 0; i < 3000; i++) {
        if (mesh_mailbox_get(MB, mesh_prog_entry(0) + MESH_PT_STATE) == st) return true;
        mesh_sleep_us(1000);
    }
    return false;
}

int main(void) {
    char root[64], path[256];
    snprintf(root, sizeof root, "/tmp/mesh-driver-place-%d", (int)getpid());
    mkdir(root, 0700);
    snprintf(path, sizeof path, "%s/dm", root);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/dm/program.toml", root);
    FILE *f = fopen(path, "w");
    fputs("name = \"dm\"\nabi = 6\nview_bytes = 0x4000\nview_abi = 2\nnodes = 1\npolicy = \"periodic\"\nhz = 10\nin_flight = \"batch\"\nfanout = 4\ninput = 64\nautostart = true\n"
          "backends = [\"wasm\"]\nplace = \"split\"\nplace_state = [\"0x0:0x24\", \"0x28:0x4\", \"0x100:0x400\"]\nplace_frame = 0x20\n"
          "place_out = [\"0x1000:0x200\", \"0x1400:0x200\", \"0x1800:0x200\", \"0x1c00:0x200\"]\nplace_show = 0x2000\n",
          f);
    fclose(f);
    mesh_log_set_fd(-1);
    MB = mesh_mailbox_test();
    static _Atomic bool quit;
    SH = mesh_shared_new(MB, root, true, 0, 2, &quit);
    SH->link_out[1] = mesh_link_queue_new(64);
    SH->bulk_out[1] = mesh_link_queue_new(8);
    mesh_backend *gpu = calloc(1, sizeof *gpu);
    gpu->ops = &BOPS;
    mesh_driver_config cfg = {true, NULL, false, false};
    mesh_driver *d = mesh_driver_new(SH, gpu, NULL, 0, cfg);
    char *err = NULL, *text = NULL;
    CHECK(mesh_driver_start(d, &err));
    void *arg[2] = {0, d};
    int code = -1;
    arg[0] = &code;
    pthread_t th;
    pthread_create(&th, NULL, run, arg);
    if (!CHECK(load("dm", &text))) fprintf(stderr, "load dm: %s\n", text);
    free(text);
    CHECK(wait_state(MESH_ST_RUNNING));

    /* Local at first; the report. */
    CHECK(place("dm", NULL, &text));
    CHECK(strncmp(text, "local role Local system 0 paused false shown 0 lat_n 0 lat_sum_us 0 lat_max_us 0 ignored 0 part_ns 0 uptime_ms ", 106) == 0);
    free(text);
    /* The leader with the other member down: pin here, at once. */
    fleet_view(1, true, 0);
    CHECK(place("dm", "pin 0", &text));
    free(text);
    CHECK(place("dm", NULL, &text));
    CHECK(strncmp(text, "pin 0 role Runner system 0 ", 27) == 0);
    free(text);
    CHECK_U(mesh_mailbox_get(MB, mesh_prog_ext(0) + MESH_PX_PLACE), MESH_PLACE_PIN);
    CHECK_U(p_run(), 1);
    CHECK(!place("dm", "pin 7", &text));
    CHECK(strstr(text, "7") != NULL);
    free(text);

    /* A follower now: the leader (system 1) coordinates. */
    fleet_view(3, true, 1);
    CHECK(!place("dm", "pin 0", &text));
    CHECK_S(text, "peer: placement changes are coordinated by the leader, system 1");
    free(text);
    /* PL_SET pin 1 from the leader with its state: written here, the kernel stops, PL_DONE back. */
    size_t st_len = 0x24 + 0x4 + 0x400;
    pl_set(mpm_pin(1), 9, 3, 1, st_len);
    mesh_link_frame fr;
    int ty;
    while ((ty = next_frame(&fr)) >= 0 && ty != MESH_F_PLACE) free(fr.b);
    CHECK_I(ty, MESH_F_PLACE);
    if (ty == MESH_F_PLACE) {
        size_t rest = 0;
        mpw_place got;
        CHECK(mpw_place_rest_len(fr.b + 1, &rest, &err) && mpw_place_decode(fr.b + 1, fr.b + 1 + MPW_PLACE_HEAD, rest, &got, &err));
        CHECK_U(got.op, MESH_PL_DONE);
        CHECK_U(got.id, 9);
        mpw_place_free(&got);
        free(fr.b);
    }
    CHECK(wait_state(MESH_ST_LOADED));
    CHECK_U(p_run(), 0);
    uint32_t view = mesh_mailbox_get(MB, mesh_prog_entry(0) + MESH_PT_VIEW_OFF);
    uint8_t b[4];
    mesh_mailbox_read(MB, view + 0x100, b, 4);
    CHECK(b[0] == (uint8_t)(0x28 * 7 + 1) && b[1] == (uint8_t)(0x29 * 7 + 1)); /* the third region's first bytes */
    CHECK(!ask(MESH_CMD_PROG_RUN, "dm", true, &text));
    CHECK_S(text, "dm is placed (pin 1): system 1 runs it; `place dm local` first, or change it there");
    free(text);
    CHECK(!ask(MESH_CMD_RESET, "dm", false, &text));
    CHECK_S(text, "dm is placed (pin 1): system 1 runs it; `place dm local` first, or change it there");
    free(text);
    /* The inventory counts it run (its placement stopped it). */
    mesh_reply *r = mesh_reply_new();
    mesh_cmd inv = mesh_cmd_new(MESH_CMD_INVENTORY);
    inv.reply = mesh_reply_sender(r);
    mesh_chan_send(SH->cmd, &inv);
    mesh_result x;
    CHECK_I(mesh_reply_wait(r, 10000, &x), MESH_CHAN_OK);
    mesh_reply_release(r);
    const mesh_actuals *a = x.ptr;
    CHECK(a && a->n == 1 && a->v[0].run);
    mesh_result_free(&x);
    /* Input goes to system 1. */
    mesh_cmd in = mesh_cmd_new(MESH_CMD_INPUT);
    in.name = strdup("dm");
    in.bytes = (uint8_t *)strdup("wasd");
    in.len = 4;
    CHECK(mesh_shared_ask(SH, in, 10000, &text));
    CHECK_S(text, "4 bytes to system 1");
    free(text);
    while ((ty = next_frame(&fr)) >= 0 && ty != MESH_F_INPUT) free(fr.b);
    CHECK_I(ty, MESH_F_INPUT);
    if (ty >= 0) free(fr.b);

    /* Back to local from the leader: it runs here again. */
    pl_set(mpm_local(), 10, 3, 1, 0);
    CHECK(wait_state(MESH_ST_RUNNING));
    CHECK_U(p_run(), 1);
    CHECK(place("dm", NULL, &text));
    CHECK(strncmp(text, "local role Local system 0 ", 26) == 0);
    free(text);
    CHECK(ask(MESH_CMD_PROG_RUN, "dm", false, &text));
    free(text);

    /* Unloaded: its placer goes with it. */
    CHECK(ask(MESH_CMD_UNLOAD, "dm", false, &text));
    free(text);
    CHECK(!place("dm", NULL, &text));
    CHECK_S(text, "no program \"dm\" (`programs` lists them)");
    free(text);

    atomic_store(&quit, true);
    pthread_join(th, NULL);
    CHECK_I(code, 0);
    mesh_driver_free(d);
    mesh_shared_free(SH);
    free(MB.base);
    unlink(path);
    snprintf(path, sizeof path, "%s/dm", root);
    rmdir(path);
    rmdir(root);
    return mesh_check_done();
}
