# @ajax/mesh-fleet-groups

The leader's lockstep group policy: a running group with a member missing pauses (its data plane paused
already); a paused group whose members are all back resumes as a new epoch (reset); with
`on_loss = reform` and a timeout, a paused group re-forms over its survivors; `group <name> reform`
makes a new epoch over every up member that can run it.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/fleet.rs`:
`new_epoch`, `group_policy`, `reform`.

## Tests

`tests/groups.c`: the Rust's `group_epochs`.

## Licence

MIT. See the repository's LICENSE.
