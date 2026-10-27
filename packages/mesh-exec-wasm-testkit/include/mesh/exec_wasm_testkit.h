/* @ajax/mesh-exec-wasm-testkit: wasm guest programs run through the real wasm executor
 * (@ajax/mesh-exec-wasm) on a test mailbox, with what meshd's driver would otherwise do written out
 * minimally: a catalog dir with program.toml and <name>.wasm, the program-table entry (name, state,
 * view in the view heap, nodes, peer mode, reads mask, windows, param), the launch, tickets
 * published on the slot's ring, and the shutdown. For tests; not meshd. */
#ifndef MESH_EXEC_WASM_TESTKIT_H
#define MESH_EXEC_WASM_TESTKIT_H

#include "mesh/backend.h"
#include "mesh/exec_wasm.h"
#include "mesh/mailbox.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool used;
    mesh_kernel *k;
    mesh_stream *st;
    char *name;
    uint32_t nodes;
    uint32_t next;   /* the next ticket */
    size_t view;     /* mailbox offset of the view */
    uint32_t view_bytes;
} mesh_kit_slot;

typedef struct {
    mesh_mailbox mb;
    mesh_backend *b;
    char root[128]; /* the catalog: a temp dir */
    size_t heap;    /* the next free view-heap offset */
    mesh_kit_slot slot[32];
    char *cache;
} mesh_kit;

/* A fresh mailbox and executor (cache: --wasm-cache or NULL). */
bool mesh_kit_open(mesh_kit *kit, const char *cache, char **err);
void mesh_kit_close(mesh_kit *kit);

/* Prepares (through the executor's compiler: manifest checks, float guard, compile), loads and
 * launches the program of manifest text and wasm file at slot; nodes = the manifest's. */
bool mesh_kit_load(mesh_kit *kit, uint32_t slot, const char *manifest, const char *wasm_path, char **err);
/* As mesh_kit_load with the wasm bytes given. */
bool mesh_kit_load_bytes(mesh_kit *kit, uint32_t slot, const char *manifest, const uint8_t *wasm, size_t n, char **err);
/* Sets a slot's reads mask (PT_READS_MASK) under its PT_GEN seqlock. */
void mesh_kit_reads(mesh_kit *kit, uint32_t slot, uint32_t mask);

/* Publishes (op, arg) and waits for its completion (at most timeout_ms): true with *result; false
 * on a fault (*fault holds P_FAULT) or a timeout (*fault 0). */
bool mesh_kit_task(mesh_kit *kit, uint32_t slot, uint32_t op, uint32_t arg, uint32_t *result, uint32_t *fault,
                   unsigned timeout_ms);
/* Publishes n tickets of (op, arg) back to back and waits for them all (a throughput run). */
bool mesh_kit_tasks(mesh_kit *kit, uint32_t slot, uint32_t op, uint32_t arg, uint32_t n, uint32_t *fault, unsigned timeout_ms);

/* P_SHUTDOWN, waits for the lanes to halt, unloads, clears the slot's entry and control block. */
void mesh_kit_unload(mesh_kit *kit, uint32_t slot);

/* The built module of a wasm package (its id, e.g. "ajax-mesh-prog-hello-wasm"), found beside the
 * running test binary: <out>/<profile>/wasm/<id>.wasm, where the test is <out>/<profile>/test/... .
 * NULL (and a message on stderr) when it isn't built: run `sexc build` first. malloc'd. */
char *mesh_kit_artifact(const char *id);

/* The slot's view at off. */
size_t mesh_kit_view(const mesh_kit *kit, uint32_t slot, size_t off);

#endif
