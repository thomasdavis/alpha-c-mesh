# @ajax/mesh-top-tests

Not published: crates/mesh-top's integration tests against the mock meshd (client, render, cluster, fleet, views, net, load) and the --print frames checked byte for byte against the Rust.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): Not published. crates/mesh-top's integration tests: tests/client.rs, render.rs, cluster.rs, fleet.rs, views.rs, net.rs, load.rs; and the golden frames of the scratch generator (docs/port/top.md).

## Tests

`client.c`, `render.c` (20 tests), `cluster.c` (cluster.rs and fleet.rs), `views.c`, `net.c` (net.rs and load.rs), and `golden.c`: 167 frames of every tab at many sizes, compared with `tests/data/frames.txt` cell for cell and by a hash of every cell's colours and modifiers.

## Licence

MIT. See the repository's LICENSE.
