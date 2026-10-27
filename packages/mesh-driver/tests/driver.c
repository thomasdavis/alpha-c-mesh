/* driver.rs's tests (job_setup_in_order, reads_by_name), and the driver end to end over a fake CPU
 * executor whose "nodes" are a thread playing mesh-rt's node loop on the mailbox: autoload, the blink,
 * OP_INIT, on-demand tasks, stop and start, refusals, unload, --state, shutdown. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/check.h"
#include "mesh/driver.h"
#include "mesh/log.h"
#include "mesh/manifest.h"

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

static int task(const char *name, uint32_t op, uint32_t arg, uint32_t *v, char **text) {
    mesh_reply *r = mesh_reply_new();
    mesh_cmd c = mesh_cmd_new(MESH_CMD_TASK);
    c.name = strdup(name);
    c.op = op;
    c.arg = arg;
    c.reply = mesh_reply_sender(r);
    mesh_chan_send(SH->cmd, &c);
    mesh_result x;
    int w = mesh_reply_wait(r, 10000, &x);
    mesh_reply_release(r);
    if (w != MESH_CHAN_OK) return -1;
    *v = x.value;
    *text = x.text ? strdup(x.text) : NULL;
    int ok = x.ok;
    mesh_result_free(&x);
    return ok;
}

static void *run(void *arg) {
    *(int *)((void **)arg)[0] = mesh_driver_run((mesh_driver *)((void **)arg)[1]);
    return NULL;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

int main(void) {
    /* ---- the Rust tests ---- */
    mesh_setup_state j;
    memset(&j, 0, sizeof j);
    const mj_setup setup[2] = {{5, 0}, {4, 150}};
    mesh_setup_step st = mesh_setup_state_step(&j, 7, setup, 2);
    CHECK(st.k == MESH_SETUP_PUBLISH && st.op == 5 && st.arg == 0);
    mesh_setup_state_published(&j);
    CHECK(mesh_setup_state_step(&j, 7, setup, 2).k == MESH_SETUP_WAIT);
    CHECK(mesh_setup_state_step(&j, 7, setup, 2).k == MESH_SETUP_WAIT);
    mesh_setup_state_completed(&j, 7, NULL);
    st = mesh_setup_state_step(&j, 7, setup, 2);
    CHECK(st.k == MESH_SETUP_PUBLISH && st.op == 4 && st.arg == 150);
    mesh_setup_state_published(&j);
    CHECK(mesh_setup_state_step(&j, 7, setup, 2).k == MESH_SETUP_WAIT);
    mesh_setup_state_completed(&j, 7, NULL);
    CHECK(mesh_setup_state_step(&j, 7, setup, 2).k == MESH_SETUP_READY);
    CHECK(mesh_setup_state_step(&j, 7, setup, 2).k == MESH_SETUP_READY);
    mesh_setup_state k;
    memset(&k, 0, sizeof k);
    CHECK(mesh_setup_state_step(&k, 1, setup, 2).k == MESH_SETUP_PUBLISH);
    CHECK(mesh_setup_state_step(&k, 1, setup, 2).k == MESH_SETUP_PUBLISH);
    const mj_setup s8[1] = {{6, 32}};
    st = mesh_setup_state_step(&j, 8, s8, 1);
    CHECK(st.k == MESH_SETUP_PUBLISH && st.op == 6);
    mesh_setup_state_published(&j);
    mesh_setup_state_completed(&j, 7, "old");
    CHECK(mesh_setup_state_step(&j, 8, s8, 1).k == MESH_SETUP_WAIT);
    mesh_setup_state_completed(&j, 8, NULL);
    CHECK(mesh_setup_state_step(&j, 8, s8, 1).k == MESH_SETUP_READY);
    mesh_setup_state f;
    memset(&f, 0, sizeof f);
    CHECK(mesh_setup_state_step(&f, 2, setup, 2).k == MESH_SETUP_PUBLISH);
    mesh_setup_state_published(&f);
    mesh_setup_state_completed(&f, 2, "bad op");
    st = mesh_setup_state_step(&f, 2, setup, 2);
    CHECK(st.k == MESH_SETUP_FAILED && strcmp(st.err, "bad op") == 0);
    mesh_setup_state_reset(&j);
    CHECK(mesh_setup_state_step(&j, 8, s8, 1).k == MESH_SETUP_PUBLISH);
    mesh_setup_state z;
    memset(&z, 0, sizeof z);
    CHECK(mesh_setup_state_step(&z, 3, NULL, 0).k == MESH_SETUP_READY);
    mesh_setup_state_reset(&j);
    mesh_setup_state_reset(&k);
    mesh_setup_state_reset(&f);
    mesh_setup_state_reset(&z);

    mesh_manifest m;
    char *err = NULL;
    const char *t1 = "name = \"infer\"\nabi = 6\nreads = [\"train\", \"doom\"]";
    CHECK(mesh_manifest_parse(t1, strlen(t1), &m, &err));
    const char *names[4] = {"train", NULL, "hello", "doom"};
    CHECK_U(mesh_reads_mask(&m, names, 4), 9);
    mesh_manifest_free(&m);
    const char *t2 = "name = \"http\"\nabi = 6\nreads = [\"*\"]";
    CHECK(mesh_manifest_parse(t2, strlen(t2), &m, &err));
    CHECK_U(mesh_reads_mask(&m, names, 4), MESH_READS_ALL);
    mesh_manifest_free(&m);

    /* ---- end to end ---- */
    char root[64];
    snprintf(root, sizeof root, "/tmp/mesh-driver-%d", (int)getpid());
    char path[256];
    mkdir(root, 0700);
    snprintf(path, sizeof path, "%s/hello", root);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/hello/program.toml", root);
    write_file(path, "name = \"hello\"\nabi = 6\nview_bytes = 0x1000\nsnap_bytes = 0x40\nnodes = 2\npolicy = \"on_demand\"\nautostart = true\nbackends = [\"wasm\"]\n");
    snprintf(path, sizeof path, "%s/gpu", root);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/gpu/program.toml", root);
    write_file(path, "name = \"gpu\"\nabi = 6\nbackends = [\"cuda\"]\nautostart = true\n");
    char state[256];
    snprintf(state, sizeof state, "%s/state", root);
    mesh_log_set_fd(-1);

    MB = mesh_mailbox_test();
    static _Atomic bool quit;
    SH = mesh_shared_new(MB, root, false, 0, 1, &quit);
    mesh_backend *gpu = calloc(1, sizeof *gpu);
    gpu->ops = &BOPS;
    mesh_driver_config cfg = {false, state, true, false};
    mesh_driver *d = mesh_driver_new(SH, gpu, NULL, 0, cfg);
    CHECK(mesh_driver_start(d, &err));
    void *arg[2] = {0, d};
    int code = -1;
    arg[0] = &code;
    pthread_t th;
    pthread_create(&th, NULL, run, arg);
    /* The autoload: hello loads and starts (OP_INIT first); gpu is not loaded here (unsupported). */
    for (int i = 0; i < 2000 && !atomic_load(&SH->ready); i++) mesh_sleep_us(1000);
    CHECK(atomic_load(&SH->ready));
    for (int i = 0; i < 2000 && mesh_mailbox_get(MB, mesh_prog_entry(0) + MESH_PT_STATE) != MESH_ST_RUNNING; i++) mesh_sleep_us(1000);
    CHECK_U(mesh_mailbox_get(MB, mesh_prog_entry(0) + MESH_PT_STATE), MESH_ST_RUNNING);
    CHECK_U(mesh_mailbox_get(MB, MESH_H_MAGIC), MESH_MAGIC);
    uint32_t v = 0;
    char *text = NULL;
    CHECK_I(task("hello", MESH_OP_TASK, 5, &v, &text), 1);
    CHECK_U(v, 22);
    free(text);
    mesh_sleep_us(5000);
    CHECK_U(mesh_mailbox_get(MB, mesh_mailbox_get(MB, mesh_prog_entry(0) + MESH_PT_VIEW_OFF)), 0x4f4c4548u);
    /* Refusals. */
    CHECK(!load("hello", &text));
    CHECK_S(text, "name exists: hello is loaded");
    free(text);
    CHECK(!load("../etc", &text));
    CHECK(strncmp(text, "manifest: ", 10) == 0);
    free(text);
    CHECK(!load("gpu", &text));
    CHECK_S(text, "unsupported: gpu runs on cuda (manifest `backends`); this meshd runs wasm");
    free(text);
    CHECK(!ask(MESH_CMD_ROLLBACK, "hello", false, &text));
    CHECK_S(text, "hello: no previous image to roll back to");
    free(text);
    CHECK(!ask(MESH_CMD_PROG_RUN, "nope", true, &text));
    CHECK_S(text, "no program \"nope\" (`programs` lists them)");
    free(text);
    CHECK_I(task("nope", 0, 0, &v, &text), 0);
    CHECK_S(text, "no program \"nope\" (`programs` lists them)");
    free(text);
    /* Stop and start: zero gap for the others, answered once halted / online. */
    CHECK(ask(MESH_CMD_PROG_RUN, "hello", false, &text));
    CHECK(strstr(text, " us") != NULL);
    free(text);
    CHECK_U(mesh_mailbox_get(MB, mesh_prog_entry(0) + MESH_PT_STATE), MESH_ST_LOADED);
    CHECK_I(task("hello", MESH_OP_TASK, 1, &v, &text), 0);
    CHECK_S(text, "hello is not running");
    free(text);
    CHECK(ask(MESH_CMD_PROG_RUN, "hello", true, &text));
    free(text);
    CHECK_I(task("hello", MESH_OP_TASK, 10, &v, &text), 1);
    CHECK_U(v, 37);
    free(text);
    /* A bad op: the program's answer. */
    CHECK_I(task("hello", 9, 0, &v, &text), 1);
    CHECK_U(v, MESH_R_BAD_OP);
    free(text);
    /* Reset while running: a blink of its slot; the view zeroed, OP_INIT again. */
    CHECK(ask(MESH_CMD_RESET, "hello", false, &text));
    CHECK(strncmp(text, "blink ", 6) == 0);
    free(text);
    /* --state records it. */
    mesh_sleep_us(20000);
    FILE *sf = fopen(state, "r");
    char line[256] = "";
    if (sf) {
        char *got = fgets(line, sizeof line, sf);
        got = fgets(line, sizeof line, sf);
        (void)got;
        fclose(sf);
    }
    CHECK_S(line, "hello hello 1\n");
    /* Unload. */
    CHECK(ask(MESH_CMD_UNLOAD, "hello", false, &text));
    CHECK_S(text, "");
    free(text);
    CHECK_U(mesh_mailbox_get(MB, mesh_prog_entry(0) + MESH_PT_STATE), MESH_ST_EMPTY);
    /* Loaded again into the freed slot (the blink frees the old view). */
    CHECK(load("hello", &text));
    CHECK(strncmp(text, "slot 0 blink ", 13) == 0);
    free(text);
    atomic_store(&quit, true);
    pthread_join(th, NULL);
    CHECK_I(code, 0);
    mesh_driver_free(d);
    mesh_shared_free(SH);
    free(MB.base);
    char cmd[300];
    snprintf(cmd, sizeof cmd, "%s/hello/program.toml", root);
    unlink(cmd);
    snprintf(cmd, sizeof cmd, "%s/gpu/program.toml", root);
    unlink(cmd);
    unlink(state);
    snprintf(cmd, sizeof cmd, "%s/hello", root);
    rmdir(cmd);
    snprintf(cmd, sizeof cmd, "%s/gpu", root);
    rmdir(cmd);
    rmdir(root);
    return mesh_check_done();
}
