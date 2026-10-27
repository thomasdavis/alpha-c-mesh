# @ajax/tui-input

Key events from terminal input bytes (arrows, function keys, Ctrl, UTF-8), as crossterm decodes them.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows crossterm 0.29's key decoding (`event/sys/unix/parse.rs`) for the keys mesh-top uses: characters (UTF-8), Enter, Esc, Backspace, Tab, BackTab, arrows, Home/End, PageUp/PageDown, Insert/Delete, F1-F12, with Shift/Control/Alt. Mouse and focus reports are consumed and ignored.

## Tests

`tests/input.c`.

## Licence

MIT. See the repository's LICENSE.
