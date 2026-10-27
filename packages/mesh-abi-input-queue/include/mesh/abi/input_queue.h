/* @ajax/mesh-abi-input-queue: A program's input queue in its view (IQ_*).
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_INPUT_QUEUE_H
#define MESH_ABI_INPUT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The input queue at view PT_INPUT_OFF: the host appends bytes at
 * IQ_DATA + (head % len) and then stores head + n (release); the program
 * consumes from tail (`Ctx::input_pop`).  Occupies IQ_DATA + len bytes. */
#define MESH_IQ_HEAD 0x0 /* u32 host */
#define MESH_IQ_TAIL 0x4 /* u32 GPU */
#define MESH_IQ_DATA 0x10

#endif
