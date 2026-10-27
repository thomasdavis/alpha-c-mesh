# @ajax/mesh-top-ui-fleet

mesh-top's fleet tabs: Fleet (members, placement matrix, explain, decisions, lockstep groups), Jobs (table, per-member shares, results, lease choice), Pipes (inboxes and feeds), and the fleet from the snapshots alone.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/ui_fleet.rs` and `src/ui.rs` (draw_fleet_snap).

## Tests

@ajax/mesh-top-tests' golden replay draws it in 167 frames byte-identical to the Rust's (text and styles); @ajax/mesh-top-tests ports the Rust assertions on it.

## Licence

MIT. See the repository's LICENSE.
