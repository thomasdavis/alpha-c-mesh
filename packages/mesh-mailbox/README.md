# @ajax/mesh-mailbox

Host views of the mailbox words: acquire loads, relaxed payload stores, release publishes, atomic adds and compare-and-swap, 64-bit and byte access, word-wise byte windows, and the pipe words (`mesh_words`) over it. A bad offset aborts with the offset, as the Rust's assert panics.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/mailbox.rs.

## Tests

`tests/mailbox.c`: every accessor, windows with padding, and the pipe words.

## Licence

MIT. See the repository's LICENSE.
