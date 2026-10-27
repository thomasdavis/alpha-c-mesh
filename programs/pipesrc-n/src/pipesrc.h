/* pipesrc-n's init and task (src/main.c runs them as a native worker). */
#ifndef PIPESRC_N_H
#define PIPESRC_N_H
#include "mesh/worker_ctx.h"
void pipesrc_init(const mesh_worker_ctx *ctx, void *user);
uint32_t pipesrc_task(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *user);
#endif
