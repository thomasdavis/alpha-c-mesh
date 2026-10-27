/* fleet.rs: new_epoch, group_policy, reform. */
#include "mesh/fleet_groups.h"
#include "mesh/text.h"

void mfg_new_epoch(mfd_group *g, ml_list members) {
    g->epoch += 1;
    g->cycle = 0;
    g->op = (mfd_op){MFD_OP_NONE, 0, 0};
    ml_free(&g->members);
    g->members = members;
    g->state = MFD_RUNNING;
}

bool mfg_policy(const mfd_group *g, uint32_t up, int64_t paused_ms, mfd_group *out) {
    bool all = (mfd_group_mask(g) & ~up) == 0;
    if (g->state == MFD_RUNNING && !all) {
        *out = mfd_group_clone(g);
        out->state = MFD_PAUSED;
        return true;
    }
    if (g->state == MFD_PAUSED && all && g->members.n) {
        *out = mfd_group_clone(g);
        mfg_new_epoch(out, ml_clone(&g->members));
        return true;
    }
    if (g->state == MFD_PAUSED && g->on_loss == MFD_LOSS_REFORM && g->auto_ms > 0 && paused_ms >= 0 && (uint64_t)paused_ms >= g->auto_ms) {
        ml_list survivors = {0};
        for (size_t i = 0; i < g->members.n; i++)
            if (up >> g->members.v[i] & 1) ml_push(&survivors, g->members.v[i]);
        if (!survivors.n) return false;
        *out = mfd_group_clone(g);
        mfg_new_epoch(out, survivors);
        return true;
    }
    return false;
}

bool mfg_reform(const mfd_group *g, const ml_list *eligible_up, mfd_group *out, char **err) {
    if (!eligible_up->n) {
        *err = mt_fmt("group %s: no member that can run %s is up", g->name, g->program);
        return false;
    }
    *out = mfd_group_clone(g);
    mfg_new_epoch(out, ml_clone(eligible_up));
    return true;
}
