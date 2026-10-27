# @ajax/mesh-top-app

mesh-top's viewer state: one machine per meshd endpoint with the history rates come from, tabs, focus, prompts, and every key binding.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/app.rs`.

## Tests

`tests/build.c`; the key bindings and state are exercised by @ajax/mesh-top-tests (render.rs, cluster.rs, views.rs ported) and its golden replay.

## Licence

MIT. See the repository's LICENSE.
