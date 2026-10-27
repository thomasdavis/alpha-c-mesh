# @ajax/mesh-fleet-core

The fleet's election and commit state machine (`mfc_core`), sans-IO: Raft's rules for a replicated
document sent whole (so the log is one (term, version) pair), with PreVote (a member that was cut off
cannot disturb a working leader when it returns), CheckQuorum (a leader that loses its majority steps
down), one change in flight at a time, catch-up of members that were away, and a fixed coordinator for
a pair (N = 2 has no majority after a loss). `mfc_durable` writes every Persist before anything after
it goes out, and stops at the first failed write (fail-stop).

The shell (`@ajax/mesh-peer-relay`) feeds it link changes, ticks and messages; the messages' wire
form is `@ajax/mesh-peer-codec`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/fleet.rs`: `Msg`,
`Role`, `Config`, `Out`, `durable`, `Core`.

## Tests

- `tests/election.c`: the Rust's simulated network, ported (`Sim`): a leader is elected and commits
  (N = 3, 4, 5), a minority refuses changes, a 2 + 2 split has no leader, the pair's fixed coordinator,
  terms and votes survive restarts, and the randomised run over 300 seeds (partitions, crashes,
  restarts, message loss, disks that fill up) asserting at most one leader per term, one document per
  committed version and per accepted (term, version), no committed change lost, no committed document
  going back. `election 2000` runs the Rust's ignored 2000-seed variant (7469 leaderships, 23538
  committed versions, 1308 fail-stops, no invariant broken).
- `tests/catchup.c`: the Rust's three catch-up scenarios after leader changes (review finding 1) and
  `nothing_leaves_without_its_persist` (review finding 4).

## Licence

MIT. See the repository's LICENSE.
