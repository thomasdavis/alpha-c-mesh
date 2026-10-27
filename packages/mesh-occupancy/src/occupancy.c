/* backend.rs's co_resident and room. */
#include "mesh/occupancy.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

bool mesh_co_resident(const mesh_occ_kernel *k, size_t n, uint32_t sms, uint32_t blocks_per_sm, char **err) {
    uint32_t blocks = 0, whole = 0, small = 0;
    double share = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t nodes = k[i].nodes, per_sm = k[i].per_sm;
        if (per_sm == 0 || nodes > per_sm * sms)
            return mesh_errf(err, "%u blocks; its occupancy allows %u per SM x %u SMs", (unsigned)nodes, (unsigned)per_sm, (unsigned)sms);
        blocks += nodes;
        share += (double)nodes / (double)(per_sm * sms);
        if (per_sm == 1) whole += nodes;
        else small += nodes;
    }
    if (blocks > blocks_per_sm * sms)
        return mesh_errf(err, "%u resident blocks > %u per SM x %u SMs", (unsigned)blocks, (unsigned)blocks_per_sm, (unsigned)sms);
    if (share > 1.0 + 1e-9) {
        /* Rust's {:.0}: round half to even, as printf's %.0f. */
        return mesh_errf(err, "the loaded kernels would need %.0f%% of the SMs' resident capacity", share * 100.0);
    }
    uint32_t spread = small < sms ? small : sms;
    if (whole > 0 && whole + spread > sms)
        return mesh_errf(err, "%u node(s) that each need a whole SM, and %u other block(s) that may occupy %u SMs, on %u SMs",
                         (unsigned)whole, (unsigned)small, (unsigned)spread, (unsigned)sms);
    return true;
}

uint32_t mesh_occ_room(const mesh_occ_kernel *others, size_t n, uint32_t per_sm, uint32_t max, uint32_t sms, uint32_t blocks_per_sm) {
    mesh_occ_kernel *k = mesh_alloc((n + 1) * sizeof *k);
    if (n) memcpy(k, others, n * sizeof *k);
    k[n] = (mesh_occ_kernel){0, per_sm};
    uint32_t got = 0;
    while (got < max) {
        k[n].nodes = got + 1;
        if (!mesh_co_resident(k, n + 1, sms, blocks_per_sm, NULL)) break;
        got++;
    }
    free(k);
    return got;
}
