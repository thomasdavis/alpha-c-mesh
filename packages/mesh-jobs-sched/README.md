# @ajax/mesh-jobs-sched

The fleet scheduler's model as the jobs' dispatcher reads it:

- every member, by id, with the owner policy's busy marks;
- each program's needs from the desired state;
- the measured performance per program and member;
- the weights.

The fleet thread refreshes it every second and gives it to the job node (`mj_node_set_sched`) as an
`mj_sched` (`@ajax/mesh-jobs-select`). Its verdict for an alive member that runs the program is F4's
`sched::filter` (cordon, backends, arch, memory, avoid), then a failure of the program there, then
`score_terms` (`@ajax/mesh-sched-score`): measured speed against the best anywhere, share, CPU and
memory headroom, prefer, the owner penalty.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `SchedCx`, and the scheduler half of `select`.

## Tests

`tests/sched.c`: the Rust's `select_is_the_schedulers_filter_and_score`. It covers cordoned, busy
backed off, not running it, down, the busy member alone, backends, a failure there, the job's members,
and ordering by measured speed and share.

## Licence

MIT. See the repository's LICENSE.
