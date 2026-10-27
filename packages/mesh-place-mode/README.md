# @ajax/mesh-place-mode

How a program is placed across a cluster of N machines: `local`, pinned to one machine (`pin 2`), or
split by an assignment of its parts to systems (`split`, `split 0,1`, `split 1:3,2:1`, and `split auto`,
weighted by each machine's measured time per part); the wire codes (PLACE_*, PX_PLACE_ASSIGN); and each
machine's role in a mode (local, runner, mirror, owner, follower), where input goes, which parts it
renders, and whose state continues across a change.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/place.rs`:
`Assign` (`new`, `owner`, `mask_of`, `members`, `list`, `counts`, `even`, `auto`, `check`), `mask`,
`Mode` (`parse`, `code`, `assign_bytes`, `from_code`, `label`, `allowed`, `fits`), `Target`, `Role`,
`role`, `runs`, `input_to`, `parts_of`, `authority`.

## Tests

`tests/mode.c`: the Rust's `modes_and_assignments`, `roles_input_authority` and
`auto_weights_the_slow_machine_down`, with the error messages.

## Licence

MIT. See the repository's LICENSE.
