/* @ajax/mesh-fleet-persist: what a fleet member keeps on disk, written before any vote,
 * acknowledgement or commit leaves it (fleet.rs Persist): its term, its vote in that term, the
 * last document it accepted from a leader and the last it knows committed. */
#ifndef MESH_FLEET_PERSIST_H
#define MESH_FLEET_PERSIST_H

#include "mesh/fleet_doc.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t term;
    bool has_voted;
    uint32_t voted;
    mfd_doc accepted, committed;
} mfp_persist;

mfp_persist mfp_clone(const mfp_persist *p);
bool mfp_eq(const mfp_persist *a, const mfp_persist *b);
void mfp_free(mfp_persist *p);
/* The state file's text for a cluster of n. */
char *mfp_encode(const mfp_persist *p, uint32_t n);
/* Refuses a file written for another cluster size, and one without both documents. */
bool mfp_decode(const char *text, uint32_t n, mfp_persist *out, char **err);

#endif
