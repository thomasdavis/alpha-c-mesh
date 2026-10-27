/* What the executor's compiler and backend share. */
#ifndef MESH_EXEC_WASM_INTERNAL_H
#define MESH_EXEC_WASM_INTERNAL_H

#include "mesh/exec_wasm.h"

extern const mesh_kernel_ops mesh_wasm_kernel_ops;
extern const mesh_compiler_ops mesh_wasm_compiler_ops;

/* The instance's memory export and its entry points (mesh_init, mesh_task). False with *err. */
bool mesh_wasm_entry_points(const mesh_wasm_program *p, wasm_instance *inst, uint32_t *init, uint32_t *task, char **err);
/* Drops one reference; the last frees the program. */
void mesh_wasm_program_free(mesh_wasm_program *p);

#endif
