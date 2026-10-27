/* pipesink's init and task (src/main.c runs them as a native worker). */
#ifndef PIPESINK_H
#define PIPESINK_H
#include "mesh/worker_ctx.h"
void pipesink_init(const mesh_worker_ctx *ctx, void *user);
uint32_t pipesink_task(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *user);
#endif
