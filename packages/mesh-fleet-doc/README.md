# @ajax/mesh-fleet-doc

The fleet's desired state (`mfd_doc`): the programs that should be loaded (catalog dir, image hash, run
state, reset generation, the previous image for a rollback, the scheduling needs, the scheduler's
placement, a migration in flight, the operator's pin, why it is where it is), the lockstep groups
(program, ordered members, epoch, cycle, state, on-loss policy), the placement requests and the
scheduler's switches (paused, autoreform, cordon). `mfd_encode` / `mfd_decode` are the Rust's text
document, one line per item, byte for byte; the leader sends it whole.

Programs and placement requests are kept sorted by name (the Rust's `BTreeMap` order); groups keep
their order.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/fleet.rs`:
`ProgSpec` (`here`), `CycleOp`, `GroupState`, `OnLoss`, `Group` (`mask`, `rank`, `order`), `Doc`
(`id`, `group_of`, `encode`, `decode`), `can_run`.

## Tests

`tests/doc.c`: the document part of the Rust's `document_round_trips` (with every scheduler field),
the exact text, decode errors in the Rust's words and order, ranks, `here`, `can_run`.

## Licence

MIT. See the repository's LICENSE.
