/* @ajax/mesh-top-clock: Rust's Instant and Duration as mesh-top uses them. An instant is
 * nanoseconds on CLOCK_MONOTONIC (or on a virtual clock, for tests that must be repeatable);
 * durations convert to seconds exactly as Duration::as_secs_f32/as_secs_f64 do, so derived rates
 * round the same as the Rust's. */
#ifndef MESH_TOP_CLOCK_H
#define MESH_TOP_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t mt_instant;  /* ns */
typedef uint64_t mt_duration; /* ns */

#define MT_MS(n) ((mt_duration)(n) * 1000000u)
#define MT_SECS(n) ((mt_duration)(n) * 1000000000u)

/* Now: the monotonic clock, or the virtual one while it is on. */
mt_instant mt_now(void);
/* Virtual time for tests: on, at ns. */
void mt_clock_virtual(bool on, uint64_t ns);
/* Instant::duration_since (saturating: an earlier "now" is a zero duration). */
static inline mt_duration mt_since(mt_instant now, mt_instant earlier) { return now > earlier ? now - earlier : 0; }
/* Duration::as_secs_f32 / as_secs_f64. */
static inline float mt_secs_f32(mt_duration d) {
    return (float)(d / 1000000000u) + (float)(uint32_t)(d % 1000000000u) / 1e9f;
}
static inline double mt_secs_f64(mt_duration d) {
    return (double)(d / 1000000000u) + (double)(uint32_t)(d % 1000000000u) / 1e9;
}
/* Duration::from_secs_f32 (to the nearest ns). */
mt_duration mt_from_secs_f32(float s);

#endif
