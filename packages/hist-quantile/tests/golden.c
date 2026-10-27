/* @ajax/hist-quantile's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "hist_quantile.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(0)), 1u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(1)), 1u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(500)), 1u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(900)), 3u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(990)), 3u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(999)), 3u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(1000)), 3u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(5000)), 3u); }
    { const uint32_t h[] = {0u, 2u, 1u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(18446744073709551615)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(0)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(1)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(500)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(900)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(990)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(999)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(1000)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(5000)), 0u); }
    { const uint32_t h[] = {0u, 0u, 0u, 0u}; CHECK_U(hist_quantile(h, 4, UINT64_C(18446744073709551615)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(0)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(1)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(500)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(900)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(990)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(999)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(1000)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(5000)), 0u); }
    { const uint32_t h[] = {5u}; CHECK_U(hist_quantile(h, 1, UINT64_C(18446744073709551615)), 0u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(0)), 0u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(1)), 0u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(500)), 5u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(900)), 5u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(990)), 5u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(999)), 5u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(1000)), 5u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(5000)), 5u); }
    { const uint32_t h[] = {1u, 0u, 0u, 0u, 0u, 9u}; CHECK_U(hist_quantile(h, 6, UINT64_C(18446744073709551615)), 5u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(0)), 0u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(1)), 0u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(500)), 1u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(900)), 1u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(990)), 1u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(999)), 1u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(1000)), 2u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(5000)), 2u); }
    { const uint32_t h[] = {4294967295u, 4294967295u, 3u}; CHECK_U(hist_quantile(h, 3, UINT64_C(18446744073709551615)), 2u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(0)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(1)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(500)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(900)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(990)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(999)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(1000)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(5000)), 3u); }
    { const uint32_t h[] = {0u, 0u, 0u, 1u}; CHECK_U(hist_quantile(h, 4, UINT64_C(18446744073709551615)), 3u); }
    return mesh_check_done();
}
