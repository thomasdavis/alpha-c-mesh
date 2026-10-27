# @ajax/mesh-text

Growable strings (`mt_buf`), string lists (`mt_strs`), `mt_fmt`, and Rust's splitting
(`mt_split` = `split(c)`, `mt_words` = `split_whitespace()`, `mt_lines` = `lines()`,
`mt_split_once`, `mt_trim`) with Rust's exact edge cases (`"".split(',')` is one empty item).

The fleet packages build and parse their text wire formats with these, the way the Rust did with
`String` and `Vec<String>`. Allocation failure aborts.

## Ported from

The `String` / `Vec<String>` / `str` methods [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s
`mesh-host` fleet code (fleet.rs, sched.rs, cluster.rs, peer.rs) uses.

## Tests

`tests/text.c`: splitting edge cases, buffers, lists.

## Licence

MIT. See the repository's LICENSE.
