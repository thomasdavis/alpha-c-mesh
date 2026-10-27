/* @ajax/mesh-fleet-groups: a lockstep group's epochs (fleet.rs leader policies). A new epoch (a
 * resume after a pause, a reform, a reset, the first load) resets the program on every member
 * (rounds restart at 1); a cycle within an epoch (upgrade, rollback, task) keeps the step. */
#ifndef MESH_FLEET_GROUPS_H
#define MESH_FLEET_GROUPS_H

#include "mesh/fleet_doc.h"

#include <stdbool.h>
#include <stdint.h>

/* The group's next epoch over members (reset, running). Takes members. */
void mfg_new_epoch(mfd_group *g, ml_list members);
/* A membership decision for the up set (members heard within dead_ms): true with *out the new group
 * when the document should change.
 * - running, a member missing: paused (both policies);
 * - paused, every member back: a new epoch over the same members (reset);
 * - paused for auto_ms with on_loss = reform: a new epoch over the survivors.
 * paused_ms < 0: not known. */
bool mfg_policy(const mfd_group *g, uint32_t up, int64_t paused_ms, mfd_group *out);
/* `group <name> reform`: a new epoch over every up member that can run it, in system order. */
bool mfg_reform(const mfd_group *g, const ml_list *eligible_up, mfd_group *out, char **err);

#endif
