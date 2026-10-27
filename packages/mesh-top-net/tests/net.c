/* crates/mesh-top/src/net.rs's unit tests, ported: meter rates over the window, wraps, restarts,
 * the formatting. */
#include "mesh/check.h"
#include "mesh/top_net.h"

#include <math.h>
#include <string.h>

int main(void) {
    mt_meter m = {0};
    CHECK(mt_meter_per_s(&m) == 0.0);
    mt_meter_push(&m, 1000, 0);
    CHECK(mt_meter_per_s(&m) == 0.0);
    mt_meter_push(&m, 1500, 500);
    CHECK(mt_meter_per_s(&m) == 1000.0);
    for (uint64_t i = 2; i < 10; i++) mt_meter_push(&m, 1000 + 500 * i, 500 * i + 100 * i * i);
    CHECK(m.n <= 3);
    CHECK(mt_meter_per_s(&m) > 2000.0);
    mt_meter_free(&m);
    mt_meter_push(&m, 0, 0);
    mt_meter_push(&m, 100, 10);
    mt_meter_push(&m, 100, 20);
    CHECK(mt_meter_per_s(&m) == 200.0);
    mt_meter_free(&m);
    /* Wraps and restarts. */
    mt_meter_push(&m, 0, UINT64_MAX - 499);
    mt_meter_push(&m, 1000, 500);
    CHECK(mt_meter_per_s(&m) == 1000.0);
    mt_meter_push(&m, 2000, 10);
    CHECK(mt_meter_per_s(&m) == 0.0);
    mt_meter_push(&m, 3000, 1010);
    CHECK(mt_meter_per_s(&m) == 1000.0);
    mt_meter_push(&m, 500, 2000);
    CHECK(mt_meter_per_s(&m) == 0.0);
    mt_meter_push(&m, 1500, 2400);
    CHECK(mt_meter_per_s(&m) == 400.0);
    mt_meter_free(&m);
    char s[128];
    mt_human(870.0, s, sizeof s);
    CHECK_S(s, "870 B/s");
    mt_human(12345.0, s, sizeof s);
    CHECK_S(s, "12.3 kB/s");
    mt_human(1234567.0, s, sizeof s);
    CHECK_S(s, "1.2 MB/s");
    mt_human((double)NAN, s, sizeof s);
    CHECK_S(s, "0 B/s");
    mt_short(999.0, s, sizeof s);
    CHECK_S(s, "999");
    mt_short(154200.0, s, sizeof s);
    CHECK_S(s, "154k");
    mt_short(1250000.0, s, sizeof s);
    CHECK_S(s, "1.2M");
    uint64_t v[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    mt_spark(v, 8, 8, s);
    CHECK_S(s, "▁▂▃▄▅▆▇█");
    mt_spark(v, 8, 10, s);
    CHECK_S(s, "  ▁▂▃▄▅▆▇█");
    mt_spark(NULL, 0, 3, s);
    CHECK_S(s, "   ");
    return mesh_check_done();
}
