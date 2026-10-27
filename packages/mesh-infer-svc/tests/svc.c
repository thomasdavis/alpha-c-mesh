/* infer.rs's addresses_pick_the_shared_link. */
#include "mesh/check.h"
#include "mesh/infer_svc.h"

#include <stdlib.h>
#include <string.h>

static const char *keys[][2] = {{"0.peer", "192.168.177.11:61200"}, {"0.lan", "192.168.1.187"},
                                {"1.peer", "192.168.177.12:61201"}, {"1.lan", "192.168.1.177"},
                                {"2.peer", "192.168.1.176:61202"},  {"3.peer", "192.168.1.173:61203"}};

static const char *get(void *ctx, const char *k) {
    (void)ctx;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) if (!strcmp(keys[i][0], k)) return keys[i][1];
    return NULL;
}

int main(void) {
    mesh_infer_svc s;
    char *err = NULL, a[300];
    CHECK_I(mesh_infer_svc_cluster(&s, get, NULL, 0, 4, &err), 0);
    mesh_infer_svc_addr(&s, 0, 1, 101, a);
    CHECK_S(a, "192.168.177.12:61302");
    mesh_infer_svc_addr(&s, 3, 0, 101, a);
    CHECK_S(a, "192.168.1.187:61301");
    mesh_infer_svc_addr(&s, 0, 3, 102, a);
    CHECK_S(a, "192.168.1.173:61305");
    mesh_infer_svc_addr(&s, 2, 2, 100, a);
    CHECK_S(a, "127.0.0.1:61302");
    CHECK_I(mesh_infer_svc_single(&s, "127.0.0.1:7700", &err), 0);
    mesh_infer_svc_addr(&s, 0, 0, 100, a);
    CHECK_S(a, "127.0.0.1:7800");
    CHECK_I(mesh_infer_svc_cluster(&s, get, NULL, 0, 5, &err), -1);
    CHECK_S(err, "cluster: no 4.peer");
    free(err);
    CHECK_I(mesh_infer_svc_single(&s, "nope", &err), -1);
    CHECK_S(err, "\"nope\": host:port");
    free(err);
    CHECK_I(mesh_infer_svc_single(&s, "h:70000", &err), -1);
    CHECK_S(err, "\"h:70000\": bad port");
    free(err);
    return mesh_check_done();
}
