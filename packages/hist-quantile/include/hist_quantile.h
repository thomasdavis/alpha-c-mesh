/* @ajax/hist-quantile: The per-mille quantile of a histogram of unit bins.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef HIST_QUANTILE_H
#define HIST_QUANTILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The quantile per_mille/1000 of a histogram of n bins (bin i counts values
 * of i units): the first bin at which the running count reaches
 * ceil(total x per_mille / 1000) (at least 1); 0 for an empty histogram. */
uint32_t hist_quantile(const uint32_t *hist, size_t n, uint64_t per_mille);

#endif
