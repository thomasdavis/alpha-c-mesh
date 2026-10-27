/* @ajax/mesh-infer-svc: where each member's inference services listen
 * (infer.rs Svc): its peer host and port (the base: a worker listens on
 * base + its manifest's `param`) and the address members outside its subnet
 * use (`k.lan` in the cluster config). */
#ifndef MESH_INFER_SVC_H
#define MESH_INFER_SVC_H

#include <stdint.h>

#define MESH_INFER_SVC_MAX 8

typedef struct {
    uint32_t me, n;
    char hosts[MESH_INFER_SVC_MAX][256];
    char lans[MESH_INFER_SVC_MAX][256]; /* "" when the member has no k.lan */
    uint16_t bases[MESH_INFER_SVC_MAX];
} mesh_infer_svc;

/* A single meshd: its services on loopback, based at the control port (host:port). */
int mesh_infer_svc_single(mesh_infer_svc *s, const char *control, char **err);
/* A cluster member: `get(ctx, "k.peer")` per member, optional `k.lan` (NULL when absent). */
int mesh_infer_svc_cluster(mesh_infer_svc *s, const char *(*get)(void *ctx, const char *key), void *ctx, uint32_t me,
                           uint32_t n, char **err);
/* The address member `from` dials for member `to`'s service at `param`:
 * loopback for itself; `to`'s peer host when the two peer hosts share a /24;
 * else `to`'s lan host if it has one. Written to out (host:port). */
void mesh_infer_svc_addr(const mesh_infer_svc *s, uint32_t from, uint32_t to, uint32_t param, char out[300]);

#endif
