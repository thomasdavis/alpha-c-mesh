/* @ajax/hist-quantile: its tables and functions. */
#include "hist_quantile.h"

uint32_t hist_quantile(const uint32_t *hist, size_t n, uint64_t per_mille) {
    uint64_t total = 0;
    for (size_t i = 0; i < n; i++) total += hist[i];
    if (total == 0) return 0;
    /* saturating_mul, then div_ceil, then at least 1 */
    uint64_t prod = per_mille != 0 && total > UINT64_MAX / per_mille ? UINT64_MAX : total * per_mille;
    uint64_t want = prod / 1000 + (prod % 1000 != 0);
    if (want < 1) want = 1;
    uint64_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        acc += hist[i];
        if (acc >= want) return (uint32_t)i;
    }
    return (uint32_t)n - 1;
}
