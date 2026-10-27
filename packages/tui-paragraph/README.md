# @ajax/tui-paragraph

Text in a box: word-wrapped (trimmed or not) or truncated, aligned, scrolled, as ratatui's Paragraph.

## Ported from

ratatui-widgets 0.3.2 `paragraph.rs` and `reflow.rs` (WordWrapper, LineTruncator), as used by
alpha-rust-mesh's mesh-top.

## Tests

`tests/paragraph.c`: wrapping with and without trimming, long words, truncation, alignment,
scrolling, a block. The frames mesh-top draws with paragraphs are compared cell for cell with
ratatui's in `@ajax/mesh-top-golden`.
