/* @ajax/mesh-abi-load-page: The load page: the host block's load regions mirrored into the mailbox.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_LOAD_PAGE_H
#define MESH_ABI_LOAD_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/host_load.h"

/* ---------------------------------------------------------------- load page */
/* The host block's load regions (S_LOAD .. S_LOAD_END: this machine, its
 * cores, the members, the programs) mirrored into the mailbox by meshd's
 * load sampler after each sample, so GPU programs can read them (http's
 * /status, schedulers).  Byte for byte the host block's layout:
 * `load_page(S_GPU_SHARE)` is the mailbox offset of S_GPU_SHARE.
 * Informational: written with plain stores, not under a seqlock. */
#define MESH_LOAD_PAGE 0x48000

/* The mailbox offset of host-block offset s_off (S_LOAD <= s_off < S_LOAD_END). */
static inline size_t mesh_load_page(size_t s_off) { return MESH_LOAD_PAGE + s_off - MESH_S_LOAD; }

#endif
