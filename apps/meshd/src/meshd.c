/* meshd (main.rs): the host daemon of the GPU mesh. It owns the GPU backend (CUDA: the context, the
 * mailbox and every program's resident kernel), the CPU executors beside it (wasm lanes, native
 * worker processes), and the driver, which publishes tasks and runs the program lifecycle on the main
 * thread; the control, HTTP and sampler threads only talk to the driver (commands) or read the
 * mailbox. The jobs and blob threads run the job table (mesh-jobd); in a fleet, the fleet thread
 * (mesh-fleetd over mesh-peer-relay) links the members, follows the leader and reconciles. */
#include "meshd.h"

#include "mesh/abi.h"
#include "mesh/args.h"
#include "mesh/backend.h"
#include "mesh/buf.h"
#include "mesh/control.h"
#include "mesh/cuda.h"
#include "mesh/driver.h"
#include "mesh/exec_wasm.h"
#include "mesh/fleetd.h"
#include "mesh/http_relay.h"
#include "mesh/infer_svc.h"
#include "mesh/jobd/jobd.h"
#include "mesh/log.h"
#include "mesh/mps.h"
#include "mesh/mps_worker.h"
#include "mesh/native.h"
#include "mesh/nvjitlink.h"
#include "mesh/rsfmt.h"
#include "mesh/samplers.h"
#include "mesh/shared.h"
#include "mesh/shm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static _Atomic bool QUIT;

#if defined(__x86_64__)
#define ARCH "x86_64"
#elif defined(__aarch64__)
#define ARCH "aarch64"
#else
#define ARCH "unknown"
#endif

static void on_signal(int s) {
    (void)s;
    atomic_store(&QUIT, true);
}

static void *cleanup_thread(void *done) {
    mesh_backend_cleanup();
    atomic_store((_Atomic bool *)done, true);
    return NULL;
}

/* Waits for QUIT, then the limit; if the process is still there, logs it and ends it (_exit(3)). */
static void *watchdog(void *arg) {
    (void)arg;
    while (!atomic_load(&QUIT)) mesh_sleep_us(20000);
    mesh_sleep_us(10000000);
    mesh_error("shutdown: still running 10s after the signal; exiting at once (watchdog)");
    /* Cleanup is bounded, but the watchdog does not depend on it: at most a second more. */
    static _Atomic bool done;
    pthread_t t;
    if (pthread_create(&t, NULL, cleanup_thread, &done) == 0) pthread_detach(t);
    for (int i = 0; i < 100 && !atomic_load(&done); i++) mesh_sleep_us(10000);
    _exit(3);
}

static void install_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
    pthread_t t;
    if (pthread_create(&t, NULL, watchdog, NULL) != 0) { mesh_error("the watchdog thread"); abort(); }
    pthread_detach(t);
}

/* Every exit after the MPS daemon may have started quits it (a no-op in blink mode). */
static _Noreturn void fail(char *e) {
    mesh_error("%s", e);
    free(e);
    mesh_backend_cleanup();
    mesh_log_exit(1);
}

/* host:port to a listening TCP socket: the fd, or -1 with *err (std's words). */
static int listen_on(const char *addr, char **err) {
    const char *c = strrchr(addr, ':');
    if (!c) { *err = mesh_xstrdup("invalid socket address"); return -1; }
    char host[256];
    snprintf(host, sizeof host, "%.*s", (int)(c - addr), addr);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    int g = getaddrinfo(host, c + 1, &hints, &res);
    if (g != 0) { *err = mesh_buf_fmt("%s", gai_strerror(g)); return -1; }
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (bind(fd, res->ai_addr, res->ai_addrlen) != 0 || listen(fd, 128) != 0) {
        int e = errno;
        *err = mesh_buf_fmt("%s (os error %d)", strerror(e), e);
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int bind_or_exit(const char *what, const char *addr) {
    char *err = NULL;
    int fd = listen_on(addr, &err);
    if (fd < 0) {
        mesh_error("%s: cannot bind %s: %s", what, addr, err);
        free(err);
        mesh_log_exit(2);
    }
    mesh_info("%s on %s", what, addr);
    return fd;
}

typedef struct {
    mesh_shared *sh;
    int fd;
} serve_arg;

static void *control_thread(void *p) { serve_arg *a = p; mesh_control_serve(a->sh, a->fd); return NULL; }
static void *http_thread(void *p) { serve_arg *a = p; mesh_http_serve(a->sh, a->fd); return NULL; }
static void *net_thread(void *p) { mesh_net_sampler(p); return NULL; }
static void *load_thread(void *p) { mesh_load_sampler(p); return NULL; }

static void spawn(void *(*fn)(void *), void *arg) {
    pthread_t t;
    if (pthread_create(&t, NULL, fn, arg) != 0) { mesh_error("cannot start a thread: %s", strerror(errno)); abort(); }
    pthread_detach(t);
}

static const char *keys_get(void *ctx, const char *k) { return ms_get(ctx, k); }

/* The CUDA backend: nvJitLink, then the context (blink) or the MPS daemon and workers. */
static mesh_backend *open_cuda(const mesh_args *a, const mesh_shm *shm, char **err) {
    /* The program loader's JIT (no fallback to the driver's): refuse to start without it. */
    mesh_nvjitlink *jit = mesh_xcalloc(1, sizeof *jit);
    if (!mesh_nvjitlink_open(a->nvjitlink, jit, err)) { free(jit); return NULL; }
    mesh_info("nvJitLink %u.%u from %s; module loading EAGER", jit->major, jit->minor, jit->path);
    mesh_backend *b = NULL;
    if (!mesh_cuda_backend_open(a->isolation, a->has_sms ? a->sms : 0, a->mps_dir, a->has_mps_pool ? a->mps_pool : 8, shm, jit, &b, err)) return NULL;
    const mesh_caps *c = b->ops->caps(b);
    bool mps = mesh_cuda_backend_mps(b);
    mesh_info("GPU %s sm_%d%d, %u SMs, %u blocks/SM max, host-native atomics %s; programs run on %u SMs%s%s", c->name, c->cc_major, c->cc_minor,
              c->sms_total, c->blocks_per_sm,
              mesh_cuda_backend_host_atomics(b) ? "yes" : "no (each pipe inbox has a GPU-side ring head of its own)", c->sms,
              c->green ? " (green context)" : " (primary context)",
              mps ? "; isolation mps: one worker process per image" : "; isolation blink: one context");
    char *lib = mesh_cuda_check_lib(err);
    if (!lib) return NULL;
    mesh_info("CUDA driver library %s", lib);
    free(lib);
    return b;
}

_Noreturn int meshd_main(int argc, char **argv) {
    /* argv without the program name. */
    int n = argc - 1;
    char **av = argv + 1;
    uint32_t kind;
    char *err = NULL;
    if (mesh_args_backend_of(n, av, &kind, &err)) {
        fprintf(stderr, "%s\n", err);
        mesh_log_exit(2);
    }
    if (kind == MESH_BACKEND_CUDA) {
        /* First (it may re-exec): the per-machine libcuda dir, --cuda-lib or $MESH_CUDA_LIB; workers inherit it. */
        char *dir = mesh_cuda_lib_dir(n, av);
        if (!mesh_cuda_use_lib(dir, argc, argv, &err)) { mesh_error("%s", err); mesh_log_exit(2); }
        free(dir);
        /* --isolation mps: meshd starts itself as each program's worker process. */
        if (n > 0 && strcmp(av[0], "worker") == 0) mesh_mps_worker_main(n - 1, av + 1);
        /* Before anything else (no other thread exists yet): EAGER, or refuse. */
        if (!mesh_cuda_force_eager(&err)) { mesh_error("%s", err); mesh_log_exit(2); }
    }
    mesh_args a;
    if (mesh_args_parse(n, av, &a, &err)) {
        fprintf(stderr, "%s\n", err);
        mesh_log_exit(2);
    }
    install_signals();
    struct stat st;
    if (stat(a.programs, &st) != 0 || !S_ISDIR(st.st_mode)) fail(mesh_buf_fmt("--programs %s: not a directory", a.programs));

    /* Ports first: a busy port should fail before the GPU is touched. */
    int control = bind_or_exit("control", a.control);
    int web = bind_or_exit("http", a.http);
    /* The mailbox: one shared-memory file the GPU backend wraps and native workers map. */
    mesh_shm shm;
    if (!mesh_shm_new(&shm, &err)) fail(err);
    /* No fallback: the backend asked for (or the platform's) is the one that runs. */
    mesh_backend *gpu = NULL;
    if (kind == MESH_BACKEND_CUDA) gpu = open_cuda(&a, &shm, &err);
    else if (kind == MESH_BACKEND_WASM) {
        /* A CPU-only meshd: wasm lanes on the shared mailbox (native workers beside them). */
        if (!mesh_wasm_backend_open(shm.base, a.wasm_cache, &gpu, &err)) gpu = NULL;
    } else err = mesh_buf_fmt("--backend %s: this meshd (linux) was built without it", MESH_BACKENDS[kind]);
    if (!gpu) fail(err);
    const mesh_caps *caps = gpu->ops->caps(gpu);
    uint8_t *base;
    uint64_t dev;
    if (!gpu->ops->mailbox(gpu, &base, &dev, &err)) fail(err);
    if (base != shm.base) fail(mesh_xstrdup("internal: the backend's mailbox is not the shared file"));
    mesh_mailbox mb = mesh_mailbox_new(base, dev);
    mesh_info("mailbox %d bytes (shared memory, fd %d), device address 0x%llx; catalog %s", MESH_MAILBOX_BYTES, shm.fd, (unsigned long long)dev,
              a.programs);
    /* The CPU executors beside the GPU, on the same mailbox: wasm lanes (none beside itself) and native worker processes. */
    mesh_backend *cpus[2];
    size_t ncpus = 0;
    if (kind != MESH_BACKEND_WASM) {
        if (!mesh_wasm_backend_open(shm.base, a.wasm_cache, &cpus[ncpus], &err)) fail(err);
        ncpus++;
    }
    /* Where the inference services listen, and what native workers are started with. */
    uint32_t me = a.cluster ? a.topo.me : 0, nsys = a.cluster ? a.topo.n : 1;
    mesh_infer_svc svc;
    bool ok = a.has_keys ? mesh_infer_svc_cluster(&svc, keys_get, &a.keys, me, nsys, &err) == 0 : mesh_infer_svc_single(&svc, a.control, &err) == 0;
    if (!ok) fail(err);
    mesh_worker_cfg wcfg;
    memset(&wcfg, 0, sizeof wcfg);
    const char *access;
    if (kind == MESH_BACKEND_CUDA) {
        char *pipe = mesh_mps_native_pipe_dir();
        if (pipe) {
            wcfg.gpu = MESH_GPU_ACCESS_MPS;
            wcfg.mps_pipe_dir = pipe;
            wcfg.check_gpu_client = mesh_mps_check_native;
            mesh_buf q = MESH_BUF_INIT;
            mesh_buf_puts(&q, "Mps(");
            mesh_rs_debug_cstr(&q, pipe);
            mesh_buf_putc(&q, ')');
            access = mesh_buf_take(&q);
        } else { wcfg.gpu = MESH_GPU_ACCESS_BLINK; access = "Blink"; }
    } else { wcfg.gpu = MESH_GPU_ACCESS_NONE; access = "None"; }
    char *env[4] = {mesh_buf_fmt("MESH_SERVICE_BASE=%u", svc.bases[me]), mesh_buf_fmt("MESH_SYSTEM=%u", me), NULL, NULL};
    if (a.models) {
        char real[PATH_MAX];
        if (!realpath(a.models, real)) fail(mesh_buf_fmt("--models %s: %s (os error %d)", a.models, strerror(errno), errno));
        env[2] = mesh_buf_fmt("MESH_MODELS=%s", real);
    }
    wcfg.env = env;
    mesh_buf envs = MESH_BUF_INIT;
    for (int i = 0; env[i]; i++) mesh_buf_printf(&envs, "%s%s", i ? " " : "", env[i]);
    mesh_info("native workers: GPU access %s; services based at port %u (%s)", access, svc.bases[me], mesh_buf_str(&envs));
    mesh_buf_free(&envs);
    cpus[ncpus++] = mesh_native_backend_new(&shm, a.logs, &wcfg);
    char *label = mesh_backend_label(mesh_caps_word(caps));
    mesh_info("executors: %s, %snative workers (%s binaries, output to %s/<name>.log); wasm: the C interpreter, NaN canonicalisation, task "
              "deadlines%s%s",
              label, kind != MESH_BACKEND_WASM ? "wasm (CPU lanes), " : "", mesh_native_target(), a.logs,
              a.wasm_cache ? ", compiled modules cached in " : "", a.wasm_cache ? a.wasm_cache : "");
    free(label);

    uint32_t system = a.cluster ? a.topo.me : MESH_PEER_NONE;
    bool peered = a.cluster;
    /* The fleet's id and size (viewers; the http program). */
    mesh_mailbox_set(mb, MESH_H_PEER, system);
    mesh_mailbox_set(mb, MESH_H_SYSTEMS, nsys);
    /* This machine's GPU facts: the header is their one source of truth. */
    mesh_mailbox_set(mb, MESH_H_SMS, caps->sms);
    mesh_mailbox_set(mb, MESH_H_SMS_TOTAL, caps->sms_total);
    mesh_mailbox_set(mb, MESH_H_CC, (uint32_t)(caps->cc_major * 10 + caps->cc_minor));
    mesh_mailbox_set(mb, MESH_H_BLOCKS_PER_SM, caps->blocks_per_sm);
    mesh_mailbox_set(mb, MESH_H_BACKEND, mesh_caps_word(caps));
    mesh_mailbox_set(mb, MESH_H_MAX_TG_THREADS, caps->max_tg_threads);
    mesh_mailbox_set(mb, MESH_H_MAX_SHARED, caps->max_shared);
    mesh_shared *sh = mesh_shared_new(mb, a.programs, peered, peered ? system : 0, nsys, &QUIT);
    /* A fleet member's persistent state, read (and checked) before anything runs. */
    mesh_fleetd *fleet = NULL;
    bool skip_startup = false;
    if (peered && !(fleet = mesh_fleetd_open(sh, &a.topo, a.fleet_state, &skip_startup, &err))) fail(err);
    mesh_host_block *host = &sh->host;
    char hostname[256] = "";
    gethostname(hostname, sizeof hostname - 1);
    mesh_host_block_set_u32(host, MESH_S_SYSTEM, system);
    mesh_host_block_set_u32(host, MESH_S_SYSTEMS, nsys);
    mesh_host_block_set_str(host, MESH_S_ARCH, 16, ARCH);
    mesh_host_block_set_u32(host, MESH_S_SMS, mesh_mailbox_get(mb, MESH_H_SMS));
    mesh_host_block_set_u32(host, MESH_S_SMS_TOTAL, mesh_mailbox_get(mb, MESH_H_SMS_TOTAL));
    mesh_host_block_set_u32(host, MESH_S_GREEN, caps->green);
    mesh_host_block_set_u32(host, MESH_S_ISOLATION, gpu->ops->isolation(gpu));
    mesh_host_block_set_u32(host, MESH_S_HEAP_FREE, (uint32_t)MESH_VIEW_PAGES);
    mesh_host_block_set_u32(host, MESH_S_HEAP_RUN, (uint32_t)MESH_VIEW_PAGES);
    mesh_host_block_set_str(host, MESH_S_HOSTNAME, 32, hostname);
    mesh_host_block_set_str(host, MESH_S_GPU, 32, caps->name ? caps->name : "");
    mesh_host_block_set_str(host, MESH_S_PROGRAMS_DIR, 64, a.programs);
    struct sockaddr_in sa;
    socklen_t sl = sizeof sa;
    char http_addr[64];
    if (getsockname(web, (struct sockaddr *)&sa, &sl) == 0) {
        char ip[32];
        inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof ip);
        snprintf(http_addr, sizeof http_addr, "%s:%u", ip, ntohs(sa.sin_port));
    } else snprintf(http_addr, sizeof http_addr, "%s", a.http);
    mesh_host_block_set_str(host, MESH_S_HTTP_ADDR, 64, http_addr);
    sh->svc = svc;
    sh->has_svc = true;
    mesh_shared_set_peer_state(sh, a.cluster ? MESH_PEER_WAITING : MESH_PEER_SINGLE);
    /* The job table's log (replayed) and the blob store (indexed) before anything runs. */
    mjd_config jc = {a.jobs_state, a.blobs, (uint64_t)a.blobs_cap_mb << 20, peered ? a.topo.tun.dead_ms : MESH_PEER_DEAD_MS};
    mjd *jobs = mjd_open(sh, &jc, &err);
    if (!jobs) fail(err);
    mesh_control_render = mjd_render;

    /* A member with a committed desired state reconciles to it: --state / --autoload are for a fresh fleet. */
    mesh_driver_config cfg = {peered, a.state, a.autoload, skip_startup};
    mesh_driver *d = mesh_driver_new(sh, gpu, cpus, ncpus, cfg);
    if (!mesh_driver_start(d, &err)) fail(err);
    static serve_arg cs, hs;
    cs = (serve_arg){sh, control};
    hs = (serve_arg){sh, web};
    spawn(control_thread, &cs);
    spawn(http_thread, &hs);
    spawn(net_thread, sh);
    spawn(load_thread, sh);
    /* The jobs thread (its own leader when single; in a fleet the job table follows the fleet's leader)
     * and the blob thread. */
    mjd_start(jobs);
    if (fleet) {
        mpk_caps pc = mesh_fleetd_caps(sh, caps->name, ARCH);
        mesh_fleetd_start(fleet, &pc);
    }
    /* The driver runs on the main thread (it made the context current in start). */
    int code = mesh_driver_run(d);
    /* A fail-stop: the fleet state or the job log could not be made durable. */
    if (atomic_load(&sh->fatal)) code = 3;
    mesh_backend_cleanup();
    mesh_driver_free(d); /* the executors with it; the port threads only use sh, which lives until exit */
    uint64_t dropped = mesh_log_dropped();
    if (dropped) mesh_info("exit %d (%llu log lines were not written to stderr)", code, (unsigned long long)dropped);
    else mesh_info("exit %d", code);
    mesh_log_exit(code);
}
