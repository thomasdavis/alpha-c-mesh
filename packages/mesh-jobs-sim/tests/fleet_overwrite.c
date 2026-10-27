/* Review finding 5: a restarted leader never overwrites a committed result (leader.h). */
#include "leader.h"

int main(void) {
    a_restarted_leader_never_overwrites_a_committed_result();
    return mesh_check_done();
}
