/* @ajax/mesh-abi-host-jobs: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/host_jobs.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_S_JOB_HELD + 4 <= MESH_S_JOBS_END, "S_JOB_HELD + 4 <= S_JOBS_END");
_Static_assert((MESH_S_JOB_DONE % 8 == 0), "S_JOB_DONE.is_multiple_of(8)");
_Static_assert((MESH_S_JOB_DUPES % 8 == 0), "S_JOB_DUPES.is_multiple_of(8)");
_Static_assert((MESH_S_JOB_TERM % 8 == 0), "S_JOB_TERM.is_multiple_of(8)");
