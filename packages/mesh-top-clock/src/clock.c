/* The clocks. */
#include "mesh/top_clock.h"

#include <math.h>
#include <time.h>

static bool virtual_on;
static uint64_t virtual_ns;

mt_instant mt_now(void) {
    if (virtual_on) return virtual_ns;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void mt_clock_virtual(bool on, uint64_t ns) {
    virtual_on = on;
    virtual_ns = ns;
}

mt_duration mt_from_secs_f32(float s) {
    if (!(s > 0)) return 0;
    double ns = rint((double)s * 1e9); /* the f32 times 1e9 is exact in long arithmetic up to ~2^53 */
    return (mt_duration)ns;
}
