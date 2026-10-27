# @ajax/mesh-member-list

Lists of fleet members (system ids) and their masks, printed and parsed exactly as the fleet's text
formats write them: `0,2,3`, `-` for none, `all` for "every member that can" (`ml_opt` is Rust's
`Option<Vec<u32>>`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/fleet.rs`
(`list`, `parse_list`, `mask_of`, `members_of`) and `sched.rs` (`list`, `at_word`).

## Tests

`tests/member_list.c`: text and masks both ways, the Rust's error messages.

## Licence

MIT. See the repository's LICENSE.
