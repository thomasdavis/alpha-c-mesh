/* The view's layout is the Rust's (pathtrace-core `view`, values printed from the Rust), and the
 * header's own static checks hold. */
#include "pathtrace/view.h"

_Static_assert(PT_V_RGB == 0x96000u && PT_V_ACC == 0x177000u && PT_VIEW_BYTES == 0x41a000u &&
                   PT_FLEET_VIEW_BYTES == 0x96000u && PT_PAYLOAD == 0x2400u && PT_VIEW_SNAP_BYTES == 0x5600u,
               "pathtrace view layout moved");

int main(void) {
    return !(pt_view_rgb(1) == PT_V_RGB + 4 * 768 && pt_view_acc(2) == PT_V_ACC + 24 * 768 &&
             pt_view_meta(3) == PT_V_META + 96 && pt_view_payload(63) == PT_V_PAYLOAD + 63 * PT_PAYLOAD);
}
