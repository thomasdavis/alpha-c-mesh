# @ajax/mesh-fleet-edit

A change to the fleet's desired state, as the control port asks for it (`mfe_parse`: `load`, `upgrade`,
`rollback`, `unload`, `start`, `stop`, `reset`, `task`, `place`, `group <name> reform|on_loss`,
`sched pin|unpin|pause|resume|autoreform|cordon|uncordon`), the next document it makes (`mfe_edit`, with
the control port's refusals in the Rust's words), and `mfe_revert`: undo exactly what a change that
failed on every member changed, keeping what committed after it.

The leader's catalog and the up members that can run a program come in through `mfe_ctx`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/fleet.rs`: `Req`
(`parse`), `Image`, `EditCtx`, `edit`, `revert`.

## Tests

`tests/edit.c`: the Rust's `changes_edit_the_document`, `a_lockstep_upgrade_must_run_on_every_member`
and `a_revert_undoes_only_its_change`.

## Licence

MIT. See the repository's LICENSE.
