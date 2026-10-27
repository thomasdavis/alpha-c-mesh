/* train on the simulator against @ajax/mesh-ref-cubic, bitwise: the samples
 * (sin in f64), every step's params and loss alone; then a lockstep group of
 * two (two mailboxes, the exchange pages mirrored by a thread as meshd's peer
 * relay does) whose members end bitwise identical and equal to the reference
 * of a 64-sample batch. */
#include "mesh/check.h"
#include "mesh/ref/cubic.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/train.c"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#define TASKS 6

static float xs[MESH_TRAIN_SAMPLES], ys[MESH_TRAIN_SAMPLES];

static void check_samples(uint8_t *view) {
    for (uint32_t i = 0; i < MESH_TRAIN_SAMPLES; i++) {
        CHECK_U(mesh_sim_u32(view + MESH_TRAIN_T_X + 4 * i), mesh_f32_bits(xs[i]));
        CHECK_U(mesh_sim_u32(view + MESH_TRAIN_T_Y + 4 * i), mesh_f32_bits(ys[i]));
    }
}

static void alone(void) {
    uint8_t *mb = mesh_sim_mailbox();
    mesh_sim_prog p = {.name = "train", .view_bytes = MESH_TRAIN_VIEW_BYTES};
    uint8_t *view = mesh_sim_install(mb, 0, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_INIT, 0), MESH_R_NONE);
    check_samples(view);
    CHECK_U(mesh_sim_u32(view + MESH_TRAIN_T_INV_BATCH), mesh_f32_bits(1.0f / 32.0f));
    float p_ref[4] = {0, 0, 0, 0}, loss[256] = {0};
    for (uint32_t task = 0; task < TASKS; task++) {
        uint32_t s = mesh_sim_u32(view + MESH_TRAIN_T_NEXT_STEP);
        CHECK_U(mesh_sim_call(mb, 0, MESH_OP_TASK, s), MESH_TRAIN_STEPS_PER_TASK);
        for (uint32_t k = 0; k < MESH_TRAIN_STEPS_PER_TASK; k++) {
            uint32_t t = s + k;
            float sums[5];
            mesh_cubic_step_sums(p_ref, xs, ys, t, sums);
            mesh_cubic_apply(p_ref, sums, MESH_TRAIN_LR, 1.0f / 32.0f);
            loss[t % 256] = mesh_cubic_loss(sums, 1.0f / 32.0f);
        }
        for (int k = 0; k < 4; k++) CHECK_U(mesh_sim_u32(view + MESH_TRAIN_T_PARAMS + 4 * k), mesh_f32_bits(p_ref[k]));
    }
    for (uint32_t j = 0; j < 256; j++) CHECK_U(mesh_sim_u32(view + MESH_TRAIN_T_LOSS + 4 * j), mesh_f32_bits(loss[j]));
    CHECK_U(mesh_sim_u32(view + MESH_TRAIN_T_NEXT_STEP), TASKS * MESH_TRAIN_STEPS_PER_TASK);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 0) + MESH_P_COUNTERS + 4 * MESH_TRAIN_C_STEPS), TASKS * MESH_TRAIN_STEPS_PER_TASK);
    /* OP_SET_LR. */
    CHECK_U(mesh_sim_call(mb, 0, MESH_TRAIN_OP_SET_LR, mesh_f32_bits(0.25f)), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + MESH_TRAIN_T_LR), mesh_f32_bits(0.25f));
    mesh_sim_halt(mb, 0, l);
    mesh_sim_mailbox_free(mb);
}

/* The peer relay: member k's round counter and slot (of the round's parity)
 * copied into every other member's exchange page, the counter released last. */
typedef struct {
    uint8_t *mb[2];
    _Atomic int stop;
} relay;

static void *mirror(void *arg) {
    relay *r = arg;
    while (!atomic_load(&r->stop)) {
        for (uint32_t k = 0; k < 2; k++) {
            uint8_t *src = r->mb[k], *dst = r->mb[1 - k];
            uint64_t n = mesh_sim_u64(src + mesh_ex_counter(k));
            if (n > mesh_sim_u64(dst + mesh_ex_counter(k))) {
                size_t par = MESH_EX_SLOT_BYTES * (size_t)(n % 2);
                memcpy(dst + mesh_ex_slots(k) + par, src + mesh_ex_slots(k) + par, MESH_EX_SLOT_BYTES);
                mesh_sim_set_u64(dst + mesh_ex_counter(k), n);
            }
        }
        struct timespec ts = {0, 10000};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* One member's driver, as meshd's periodic publisher: tasks with arg = its
 * own T_NEXT_STEP until it reaches `until`. A round the partner was late for
 * times out (bounded) and is resumed by the next task, never recomputed. */
typedef struct {
    uint8_t *mb, *view;
    uint32_t until, tasks, timeouts;
} driver;

static void *drive(void *arg) {
    driver *d = arg;
    while (mesh_sim_u32(d->view + MESH_TRAIN_T_NEXT_STEP) < d->until && d->tasks < 1000) {
        uint32_t s = mesh_sim_u32(d->view + MESH_TRAIN_T_NEXT_STEP), r = 0;
        uint32_t t = mesh_sim_publish(d->mb, 0, MESH_OP_TASK, s);
        if (!mesh_sim_wait(d->mb, 0, t, &r, 120000)) break;
        d->tasks++;
        d->timeouts += mesh_sim_u32(d->view + MESH_TRAIN_T_ERR);
    }
    return NULL;
}

static void lockstep(void) {
    relay r = {{mesh_sim_mailbox(), mesh_sim_mailbox()}, 0};
    mesh_sim_prog p = {.name = "train", .view_bytes = MESH_TRAIN_VIEW_BYTES, .peer_mode = MESH_PEER_MODE_LOCKSTEP};
    mesh_sim_launch *l[2];
    uint8_t *view[2];
    for (uint32_t k = 0; k < 2; k++) {
        mesh_sim_set_u32(r.mb[k] + MESH_H_GROUP_RANK, k);
        mesh_sim_set_u32(r.mb[k] + MESH_H_GROUP_N, 2);
        view[k] = mesh_sim_install(r.mb[k], 0, &p);
        l[k] = mesh_sim_launch_prog(r.mb[k], 0, &p, mesh_main);
        CHECK_U(mesh_sim_call(r.mb[k], 0, MESH_OP_INIT, 0), MESH_R_NONE);
        check_samples(view[k]);
        CHECK_U(mesh_sim_u32(view[k] + MESH_TRAIN_T_INV_BATCH), mesh_f32_bits(1.0f / 64.0f));
    }
    pthread_t th, dt[2];
    pthread_create(&th, NULL, mirror, &r);
    driver d[2] = {{r.mb[0], view[0], TASKS * MESH_TRAIN_STEPS_PER_TASK, 0, 0}, {r.mb[1], view[1], TASKS * MESH_TRAIN_STEPS_PER_TASK, 0, 0}};
    for (int k = 0; k < 2; k++) pthread_create(&dt[k], NULL, drive, &d[k]);
    for (int k = 0; k < 2; k++) pthread_join(dt[k], NULL);
    /* A member cannot complete a round without the other's sums, so both stop at the same step. */
    uint32_t steps = mesh_sim_u32(view[0] + MESH_TRAIN_T_NEXT_STEP);
    CHECK_U(mesh_sim_u32(view[1] + MESH_TRAIN_T_NEXT_STEP), steps);
    CHECK(steps >= TASKS * MESH_TRAIN_STEPS_PER_TASK);
    float p_ref[4] = {0, 0, 0, 0};
    for (uint32_t t = 0; t < steps; t++) {
        float a[5], b[5], sum[5];
        mesh_cubic_step_sums(p_ref, xs, ys, 2 * t, a);
        mesh_cubic_step_sums(p_ref, xs, ys, 2 * t + 1, b);
        for (int j = 0; j < 5; j++) sum[j] = a[j] + b[j];
        mesh_cubic_apply(p_ref, sum, MESH_TRAIN_LR, 1.0f / 64.0f);
    }
    for (int k = 0; k < 4; k++) {
        CHECK_U(mesh_sim_u32(view[0] + MESH_TRAIN_T_PARAMS + 4 * k), mesh_f32_bits(p_ref[k]));
        CHECK_U(mesh_sim_u32(view[1] + MESH_TRAIN_T_PARAMS + 4 * k), mesh_f32_bits(p_ref[k]));
    }
    for (int j = 0; j < 256; j++) CHECK_U(mesh_sim_u32(view[0] + MESH_TRAIN_T_LOSS + 4 * j), mesh_sim_u32(view[1] + MESH_TRAIN_T_LOSS + 4 * j));
    if (d[0].timeouts + d[1].timeouts) fprintf(stderr, "lockstep: %u and %u rounds timed out and were resumed\n", d[0].timeouts, d[1].timeouts);
    atomic_store(&r.stop, 1);
    pthread_join(th, NULL);
    for (uint32_t k = 0; k < 2; k++) {
        mesh_sim_halt(r.mb[k], 0, l[k]);
        mesh_sim_mailbox_free(r.mb[k]);
    }
}

int main(void) {
    mesh_cubic_samples(xs, ys);
    for (uint32_t i = 0; i < MESH_TRAIN_SAMPLES; i++) {
        CHECK_U(mesh_f32_bits(train_sample_x(i)), mesh_f32_bits(xs[i]));
        CHECK_U(mesh_f32_bits(train_sin_pi(xs[i])), mesh_f32_bits(mesh_cubic_sin_pi(xs[i])));
    }
    alone();
    lockstep();
    return mesh_check_done();
}
