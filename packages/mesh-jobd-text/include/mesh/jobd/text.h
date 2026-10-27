/* @ajax/mesh-jobd-text: the jobs control answers, read from this member's committed table (and, on the
 * leader, its leases, rates and picks) (jobd.rs `jobs_text`, `job_line`, `status_text`,
 * `results_text`). */
#ifndef MESH_JOBD_TEXT_H
#define MESH_JOBD_TEXT_H

#include "mesh/jobs/node.h"

#include <stdbool.h>
#include <stdint.h>

/* What the answers need besides the node: the view, whether this member is in a fleet, now (unix ms). */
typedef struct {
    const mj_node *node;
    mj_view view;
    bool peered;
    uint64_t unix_ms;
} mjd_text_cx;

/* "leader", "leader-merging", "follower" or "single". */
const char *mjd_role(const mjd_text_cx *cx);
/* `jobs`: the header line, one line per job, the leader's rates. A new string. */
char *mjd_jobs_text(const mjd_text_cx *cx);
/* One job's line (appended to out). */
void mjd_job_line(const mjd_text_cx *cx, const mj_job *j, msb *out);
/* `job status <id>`; false with *text the error ("no job 1.2 (committed here)"). */
bool mjd_status_text(const mjd_text_cx *cx, mj_job_id id, char **text);
/* `job results <id> [from] [n]`: n of the results from the from-th on. */
bool mjd_results_text(const mjd_text_cx *cx, mj_job_id id, uint64_t from, uint64_t n, char **text);

#endif
