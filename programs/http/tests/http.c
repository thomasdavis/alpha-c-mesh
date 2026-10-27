/* http on the simulator, kcheck's check 4 and more: every route's status,
 * headers and a Content-Length equal to the body; /start's P_RUN_REQ, /load's
 * request words and their 503 / 409 / 400; /view's declared fields; /train,
 * /doom from those programs' views; /render.ppm and /render.bmp copied from a
 * pathtrace view in raster order. */
#include "mesh/check.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/http.c"

#include <stdlib.h>

#define HTTP 0
#define HELLO 3
#define TRAIN 4
#define DOOM 5
#define PT 6

static uint8_t *mb;
static char resp[1 << 20];

/* GET path (or another request line): the status code, the body in *body. */
static uint32_t get(const char *line, char **body, uint32_t *len) {
    char req[256];
    snprintf(req, sizeof req, "%s\r\nHost: x\r\n\r\n", line);
    uint32_t n = 0, code = mesh_sim_request(mb, HTTP, req, (uint32_t)strlen(req), resp, sizeof resp - 1, &n);
    CHECK(n != UINT32_MAX && n < sizeof resp);
    resp[n] = 0;
    *len = n;
    char *b = strstr(resp, "\r\n\r\n");
    CHECK(b != NULL);
    *body = b ? b + 4 : resp;
    /* The status line carries the code; Content-Length is the body's length. */
    char want[32];
    snprintf(want, sizeof want, "HTTP/1.0 %u ", code);
    CHECK(strncmp(resp, want, strlen(want)) == 0);
    char *cl = strstr(resp, "Content-Length: ");
    CHECK(cl && (uint32_t)strtoul(cl + 16, NULL, 10) == n - (uint32_t)(*body - resp));
    return code;
}

static void name(uint8_t *ext_field, const char *n) { memcpy(ext_field + MESH_FD_NAME, n, strlen(n)); }

int main(void) {
    mb = mesh_sim_mailbox();
    mesh_sim_prog hp = {.name = "http", .view_bytes = MESH_HTTP_VIEW_BYTES, .reads_mask = MESH_READS_ALL,
                        .req_off = MESH_HTTP_REQ_OFF, .req_max = MESH_HTTP_REQ_MAX, .resp_off = MESH_HTTP_RESP_OFF, .resp_max = MESH_HTTP_RESP_MAX};
    uint8_t *view = mesh_sim_install(mb, HTTP, &hp);
    mesh_sim_set_u32(mb + mesh_prog_entry(HTTP) + MESH_PT_POLICY, MESH_POLICY_ON_DEMAND);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, HTTP, &hp, mesh_main);
    CHECK_U(mesh_sim_call(mb, HTTP, MESH_OP_INIT, 0), MESH_R_NONE);
    /* hello in slot 3, with a declared field and counter names. */
    mesh_sim_prog he = {.name = "hello", .view_off = MESH_VIEW_HEAP + 0x100000, .view_bytes = 0x1000};
    uint8_t *hv = mesh_sim_install(mb, HELLO, &he);
    mesh_sim_set_u32(hv, 0x4f4c4548u);
    uint8_t *x = mb + mesh_prog_ext(HELLO);
    mesh_sim_set_u32(x + MESH_PX_FIELD_COUNT, 2);
    name(x + MESH_PX_FIELDS, "magic");
    mesh_sim_set_u32(x + MESH_PX_FIELDS + MESH_FD_TYPE, MESH_FT_U32);
    mesh_sim_set_u32(x + MESH_PX_FIELDS + MESH_FD_OFF, 0);
    name(x + MESH_PX_FIELDS + MESH_FIELD_BYTES, "a_long_field_name19");
    mesh_sim_set_u32(x + MESH_PX_FIELDS + MESH_FIELD_BYTES + MESH_FD_TYPE, MESH_FT_I32);
    mesh_sim_set_u32(x + MESH_PX_FIELDS + MESH_FIELD_BYTES + MESH_FD_OFF, 4);
    mesh_sim_set_u32(hv + 4, (uint32_t)-42);
    memcpy(x + MESH_PX_COUNTER_NAMES, "tasks", 5);

    char *body;
    uint32_t len;
    CHECK_U(get("GET / HTTP/1.0", &body, &len), 200);
    CHECK(strstr(resp, "Content-Type: text/html; charset=utf-8\r\n") != NULL);
    CHECK(strstr(body, "<h1>alpha-rust-mesh</h1>") != NULL);
    CHECK(strstr(body, "<a href=\"/view/hello\">hello</a></td><td>running</td>") != NULL);
    CHECK_U(get("GET /programs HTTP/1.0", &body, &len), 200);
    CHECK(strstr(body, "0 http running 1/1 on_demand ") != NULL);
    CHECK(strstr(body, "3 hello running 0/1 continuous 0 0.0 tasks=0\n") != NULL);
    CHECK_U(get("GET /status HTTP/1.0", &body, &len), 200);
    CHECK(strncmp(body, "programs: 2\nnodes: 2\nonline: 1\n", 31) == 0);
    CHECK(strstr(body, "\npeer: single\n") != NULL);
    CHECK(strstr(body, "\nload_samples: 0\ngpu_share: -\n") != NULL);
    CHECK_U(get("POST / HTTP/1.0", &body, &len), 405);
    CHECK(strstr(resp, "\r\nAllow: GET\r\n") != NULL);
    CHECK_S(body, "method not allowed\n");
    CHECK_U(get("GET /nope HTTP/1.0", &body, &len), 404);
    CHECK_S(body, "not found\n");
    CHECK_U(get("GET nope", &body, &len), 400);
    /* /start, /stop: the slot's P_RUN_REQ (n << 1 | on). */
    CHECK_U(get("GET /start/hello HTTP/1.0", &body, &len), 200);
    CHECK_S(body, "ok\n");
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, HELLO) + MESH_P_RUN_REQ), 3);
    CHECK_U(get("GET /stop/hello HTTP/1.0", &body, &len), 200);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, HELLO) + MESH_P_RUN_REQ), 4);
    CHECK_U(get("GET /stop/http HTTP/1.0", &body, &len), 400);
    CHECK_U(get("GET /start/nobody HTTP/1.0", &body, &len), 404);
    /* /load: request words; busy until meshd acks; conflicts and bad names. */
    CHECK_U(get("GET /load/newprog HTTP/1.0", &body, &len), 202);
    CHECK_S(body, "load requested\n");
    CHECK_U(mesh_sim_u32(view + MESH_HTTP_HV_LOAD_SEQ), 1);
    CHECK(memcmp(view + MESH_HTTP_HV_LOAD_NAME, "newprog\0", 8) == 0);
    CHECK_U(get("GET /load/other HTTP/1.0", &body, &len), 503);
    mesh_sim_set_u32(view + MESH_HTTP_HV_LOAD_ACK, 1);
    CHECK_U(get("GET /load/other HTTP/1.0", &body, &len), 202);
    mesh_sim_set_u32(view + MESH_HTTP_HV_LOAD_ACK, 2);
    CHECK_U(get("GET /load/hello HTTP/1.0", &body, &len), 409);
    CHECK_U(get("GET /load/Bad_Name HTTP/1.0", &body, &len), 400);
    /* /view: the declared fields. */
    CHECK_U(get("GET /view/hello HTTP/1.0", &body, &len), 200);
    CHECK_S(body, "program: hello\nstate: running\nview_bytes: 4096\nmagic: 1330398536\na_long_field_name19: -42\n");
    /* /train. f32 1e-5 is 9.99999974e-6 in f64, which the formatter rounds up to
     * "10.0000e-6", as the Rust does. */
    CHECK_U(get("GET /train HTTP/1.0", &body, &len), 404);
    CHECK_S(body, "not loaded\n");
    mesh_sim_prog tp = {.name = "train", .view_off = MESH_VIEW_HEAP + 0x110000, .view_bytes = MESH_TRAIN_VIEW_BYTES};
    uint8_t *tv = mesh_sim_install(mb, TRAIN, &tp);
    const float p[4] = {0.5f, -1.25f, 1e-5f, 3e9f};
    for (int k = 0; k < 4; k++) mesh_sim_set_f32(tv + MESH_TRAIN_T_PARAMS + 4 * k, p[k]);
    mesh_sim_set_u32(tv + MESH_TRAIN_T_NEXT_STEP, 3);
    mesh_sim_set_f32(tv + MESH_TRAIN_T_LOSS + 8, 0.015625f);
    CHECK_U(get("GET /train HTTP/1.0", &body, &len), 200);
    CHECK_S(body, "a: 0.500000\nb: -1.250000\nc: 10.0000e-6\nd: 3.0000e9\nstep: 3\nloss: 0.015625\n");
    /* /doom: the frame from doom's columns, 24 rows of 128. */
    mesh_sim_prog dp = {.name = "doom", .view_off = MESH_VIEW_HEAP + 0x120000, .view_bytes = MESH_DOOM_VIEW_BYTES};
    uint8_t *dv = mesh_sim_install(mb, DOOM, &dp);
    mesh_sim_set_f32(dv + MESH_DOOM_D_ROWS, 48.0f);
    mesh_sim_set_u32(dv + MESH_DOOM_D_KIND + 4 * 10, 2);
    mesh_sim_set_f32(dv + MESH_DOOM_D_HEIGHT + 4 * 10, 24.0f); /* 12 rows of 24 */
    mesh_sim_set_f32(dv + MESH_DOOM_D_DIST + 4 * 10, 1.0f);
    CHECK_U(get("GET /doom HTTP/1.0", &body, &len), 200);
    CHECK_U(strlen(body), 24 * 129);
    CHECK(body[5 * 129 + 10] == ' ' && body[6 * 129 + 10] == '#' && body[17 * 129 + 10] == '#' && body[18 * 129 + 10] == '.');
    CHECK(body[0] == ' ' && body[23 * 129] == '.');
    /* /render: no pathtrace yet, then a 64 x 48 image. */
    CHECK_U(get("GET /render HTTP/1.0", &body, &len), 404);
    CHECK_S(body, "not loaded\n");
    mesh_sim_prog pp = {.name = "pathtrace", .view_off = MESH_VIEW_HEAP + 0x130000, .view_bytes = PT_VIEW_BYTES};
    uint8_t *pv = mesh_sim_install(mb, PT, &pp);
    CHECK_U(get("GET /render HTTP/1.0", &body, &len), 200);
    CHECK_S(body, "pathtrace: no image yet (OP_INIT has not run)\n");
    mesh_sim_set_u32(pv + PT_V_MAGIC, PT_VIEW_MAGIC);
    mesh_sim_set_u32(pv + PT_V_WIDTH, 64);
    mesh_sim_set_u32(pv + PT_V_HEIGHT, 48);
    mesh_sim_set_u32(pv + PT_V_TILES, 4);
    for (uint32_t y = 0; y < 48; y++)
        for (uint32_t xx = 0; xx < 64; xx++) {
            uint32_t t = (y / 24) * 2 + xx / 32;
            mesh_sim_set_u32(pv + pt_view_rgb(t) + 4 * ((y % 24) * 32 + xx % 32), 0xff000000u | (y << 16) | (xx << 8) | (x[0] & 0));
        }
    CHECK_U(get("GET /render HTTP/1.0", &body, &len), 200);
    CHECK(strncmp(body, "pathtrace: 64x48, 4 tiles of 32x24, ", 36) == 0);
    CHECK_U(get("GET /render.ppm HTTP/1.0", &body, &len), 200);
    CHECK(strncmp(body, "P6\n64 48\n255\n", 13) == 0);
    uint8_t *px = (uint8_t *)body + 13;
    CHECK(px[3 * (5 * 64 + 7)] == 0 && px[3 * (5 * 64 + 7) + 1] == 7 && px[3 * (5 * 64 + 7) + 2] == 5);
    CHECK_U(get("GET /render.bmp HTTP/1.0", &body, &len), 200);
    CHECK(body[0] == 'B' && body[1] == 'M');
    px = (uint8_t *)body + 54; /* bottom-up rows, BGR */
    CHECK(px[3 * (0 * 64 + 7)] == 47 && px[3 * (0 * 64 + 7) + 1] == 7 && px[3 * (0 * 64 + 7) + 2] == 0);
    CHECK_U(mesh_sim_u32(view + MESH_HTTP_HV_LAST_CODE), 200);
    CHECK_U(mesh_sim_call(mb, HTTP, MESH_OP_TASK, 0), MESH_R_BAD_OP);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, HTTP) + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, HTTP, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
