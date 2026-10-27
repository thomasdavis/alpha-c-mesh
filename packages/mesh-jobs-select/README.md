# @ajax/mesh-jobs-select

Who gets a job's next lease: every member's verdict, best first. A member is rejected if it is down,
not in the job's members, or doesn't run any of the job's programs. Otherwise the fleet scheduler
decides: its filter (cordon, backends, arch, memory, avoid), a failure of the program there, then its
score (measured speed and share, CPU and memory headroom, prefer, the owner penalty). The owner
policy's backoff applies too (HTCondor's): a member held busy gets no leases while an idle member runs
the program, and only floor-sized ones when it is the only one. `mj_pick_line` is what `job status`
prints.

The scheduler is an interface (`mj_sched`). `@ajax/mesh-jobs-sched` implements it over the fleet's
model (`@ajax/mesh-sched-*`). Without one (a single meshd), a member that is alive and runs the
program is eligible, unscored.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Pick` (`ok`, `line`), `select`. `SchedCx` became the `mj_sched`
interface.

## Tests

`tests/select.c`: no fleet (alive and running it; down; not in the job's members; does not run
there), and a stub scheduler (rejected, backed off when busy, unscored, best-first order, the busy
member alone with floor-sized leases). With the fleet's own scheduler, the Rust's
`select_is_the_schedulers_filter_and_score` runs in `@ajax/mesh-jobs-sched`.

## Licence

MIT. See the repository's LICENSE.
