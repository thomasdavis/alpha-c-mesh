# Port: crates/mesh-top

alpha-rust-mesh's `crates/mesh-top` is the terminal dashboard for meshd clusters, built on ratatui 0.30.2 and
crossterm 0.29. In C it is 41 packages and 2 apps:

- 20 `tui-*` packages plus `rsfmt`. These are the parts of ratatui, kasuari, unicode-width, unicode-segmentation and
  crossterm that mesh-top uses, ported so they compute the same cells.
- 20 `mesh-top-*` packages: the state, the client, the mock meshd and every pane.
- `apps/mesh-top` and `apps/mesh-top-mock`.

**What is verified.** Every frame of the golden scenarios is byte-identical to what the Rust draws. That covers all
167 frames: every tab at many sizes, the prompts, offline machines, placements, clusters of 3 and 4, views, network
and load. The comparison includes each cell's text, colours and modifiers.

## The package graph

```
rsfmt  tui-arena  tui-utf8  tui-unicode-width  tui-grapheme  tui-style  tui-rect
   tui-buffer ─ tui-text ─ tui-block ─ tui-paragraph
   tui-solver ─ tui-layout ─ tui-table   tui-sparkline   tui-canvas ─ tui-chart
   tui-input   tui-term   tui-render                (tui-golden: private widget goldens)

mesh-abi-*, pathtrace-view
   mesh-top-clock  mesh-top-snap  mesh-top-views  mesh-top-net
   mesh-top-app (state, keys) ─ mesh-top-client (endpoint threads)
   mesh-top-mock (mock meshd + TCP control port)
   mesh-top-ui-kit ─ mesh-top-ui-{load, net} ─ mesh-top-ui-mesh
                   ─ mesh-top-ui-{programs, doom, train, infer, http, fleet, render}
   mesh-top-ui (draw, tab bar, status, prompt, flush)
apps/mesh-top (client + ui + tui-term + tui-render)   apps/mesh-top-mock (mesh-top-mock)
mesh-top-tests (private: the integration tests and the golden replay)
```

## Rust → C

| Rust | C package | C |
|---|---|---|
| ratatui-core `buffer`, `Cell` | tui-buffer | `tui_buffer`, `tui_cell`, `tui_buffer_set_stringn` |
| ratatui-core `style` | tui-style | `tui_color`, `tui_style`, `tui_style_patch` |
| ratatui-core `layout::Rect` | tui-rect | `tui_rect`, `tui_rect_inner` |
| ratatui-core `layout::Layout` + kasuari | tui-layout, tui-solver | `tui_layout_split`, `tui_vsplit`, `tui_hsplit`, `tui_solver_*` |
| ratatui-core `text` | tui-text | `tui_span`, `tui_line`, `tui_text` |
| ratatui-widgets `Block`, `Paragraph` (+ reflow) | tui-block, tui-paragraph | `tui_block_*`, `tui_paragraph_render` |
| ratatui-widgets `Table`, `Sparkline`, `Canvas`, `Chart` | tui-table, tui-sparkline, tui-canvas, tui-chart | `tui_table_render`, `tui_sparkline_render`, `tui_canvas_*`, `tui_chart_render` |
| ratatui `Terminal`, `BufferDiff`; crossterm escapes | tui-render | `tui_terminal_*`, `tui_render_diff` |
| crossterm raw mode, alternate screen, key events | tui-term, tui-input | `tui_term_*`, `tui_input_parse`, `tui_key` |
| unicode-width 0.2.2 | tui-unicode-width | `tui_char_width`, `tui_str_width` (generated tables) |
| unicode-segmentation 1.13.3 | tui-grapheme | `tui_grapheme_next` (generated tables) |
| `String::from_utf8_lossy` | tui-utf8 | `tui_utf8_lossy`, `tui_utf8_next` |
| `format!` of floats | rsfmt | `rsfmt_fixed`, `rsfmt_exp`, `rsfmt_fixed_w`, `rsfmt_pad` |
| `std::time::Instant` | mesh-top-clock | `mt_instant` (ns), `mt_now`, `mt_clock_virtual` |
| `src/snap.rs` | mesh-top-snap | `mt_snapshot_*`, `mt_section_*`, `mt_parse_trace`, `mt_quantile_us` |
| `src/views.rs` | mesh-top-views | `mt_view`, `mt_vrec_*` |
| `src/net.rs` | mesh-top-net | `mt_net_track`, `mt_meter`, `mt_human`, `mt_short`, `mt_spark` |
| `src/app.rs` | mesh-top-app | `mt_app`, `mt_machine`, `mt_app_apply`, `mt_app_handle_key`, `mt_app_tick`, `mt_update_*` |
| `src/client.rs` | mesh-top-client | `mt_endpoint_*`, `mt_updates_*`, `mt_binary_reply`, `mt_reply_timeout_ms` |
| `src/ui.rs` (palette, helpers) | mesh-top-ui-kit | `mt_frame`, `MT_*` colours, `mt_panel`, `mt_machine_title`, `mt_fmt_*` |
| `src/ui.rs` draw, tabs, status, prompt | mesh-top-ui | `mt_ui_draw`, `mt_ui_flush` |
| `src/ui.rs` "1 mesh" | mesh-top-ui-mesh | `mt_draw_mesh`, `mt_cluster_rows`, `mt_infer_spans` |
| `src/ui.rs` "2 programs" | mesh-top-ui-programs | `mt_draw_programs`, `mt_place_cell` |
| `src/ui.rs` "3 doom" | mesh-top-ui-doom | `mt_draw_doom`, `mt_render_doom_at`, `mt_shown_frame` |
| `src/ui.rs` "4 train", "5 infer", "6 http" | mesh-top-ui-train, -infer, -http | `mt_draw_train`, `mt_identity_line`, `mt_draw_infer`, `mt_draw_http` |
| `src/ui.rs` "network", "7 net" | mesh-top-ui-net | `mt_draw_net`, `mt_net_lines`, `mt_net_cells`, `mt_draw_net_box` |
| `src/ui.rs` "8 load" | mesh-top-ui-load | `mt_draw_load`, `mt_load_cells`, `mt_load_lines`, `mt_draw_load_box` |
| `src/ui_fleet.rs`, `draw_fleet_snap` | mesh-top-ui-fleet | `mt_draw_fleet`, `mt_draw_jobs`, `mt_draw_pipes`, `mt_draw_fleet_snap` |
| `src/ui_render.rs` | mesh-top-ui-render | `mt_draw_render`, `mt_hue` |
| `src/main.rs` | apps/mesh-top | the same flags, keys, poll rates (66 ms, 33 ms on Doom, 4 queued Doom keys) and `--print` |
| `tests/support/mock.rs` | mesh-top-mock | `mt_mock_world`, `mt_mock_synth`, `mt_mock_trace`, `mt_mock_request`, `mt_mock_start` |
| `examples/mock_meshd.rs` | apps/mesh-top-mock | `mesh-top-mock [ADDR] [HOST] [SYSTEM]` |

## How it is checked

### Golden vectors

The golden vectors were generated with Rust 1.88 in scratch space. Per the brief, that Rust is never committed.

- `golden.rs` is the mesh-top golden generator.
  - It is a test inside a scratch copy of the crate.
  - The mock gained a virtual clock (`mock::vclock`), so every `Instant` in a scenario is deterministic.
  - It draws every scenario of render.rs, cluster.rs, fleet.rs, views.rs, net.rs and load.rs, plus the Render tab, into
    a `TestBackend`.
  - It writes each frame's text and an FNV-1a hash of every cell's (fg, bg, modifiers). Colours are encoded as Reset
    `[0,0,0,0]`, Rgb `[1,r,g,b]`, Indexed `[2,i,0,0]` and Named `[3,n,0,0]`; modifiers as their bits in LE u16.
  - Its output is `packages/mesh-top-tests/tests/data/frames.txt`.
  - It also wrote `formats.txt`, the Rust float formatting that @ajax/rsfmt is checked against.
  - Nine runs of the generator gave identical frames.
- `layout_gold.rs` produced 3000 random layouts, in `packages/tui-layout/tests/data/layouts.txt`.
  - ratatui resolves exact ties in Cassowary by `HashMap` order, which is seeded per process, so 31 of the 3000 vary
    between Rust runs.
  - The file lists every result observed across runs. Two of these layouts (193 and 232) were checked by hand to be
    exact ties.
- `widgets_gold.rs` produced 1900 random Paragraph, Table, Sparkline and Chart configurations, in
  `packages/tui-golden/tests/data/widgets.txt`.
- `uni-gold` is a small crate that hashed `unicode-width` over every code point and `unicode-segmentation` over a mixed
  text. Those hashes are the expectations in @ajax/tui-unicode-width and @ajax/tui-grapheme.

### Tests

- The Rust's tests, ported one for one:
  - 3 from `tests/client.rs`, run against the mock over real TCP;
  - 20 from `render.rs`;
  - 7 from `cluster.rs` and 2 from `fleet.rs`;
  - 7 from `views.rs`;
  - 7 from `net.rs` and 6 from `load.rs`;
  - 4 from `snap.rs`;
  - the unit tests of `net.rs`.
- The golden replay (`mesh-top-tests/tests/golden.c`):
  - 167 of 167 frames match, text and style hash.
  - With `MT_GOLDEN_DUMP=<dir>`, it writes its frames and run-length style dumps in the Rust's format. These were
    `cmp`-identical to the Rust's `frames.txt` and `styles.txt`.
  - A corrupted fixture makes it fail.
- Every package passes `sexc test` and `CC=clang sexc test --profile asan`.
- End to end:
  - `mesh-top-mock 127.0.0.1:47791 dgx-00 0` with `mesh-top --print 2 120x30 127.0.0.1:47791` draws the Programs tab
    live over TCP.
  - The interactive mode was driven under `script(1)`: switching tabs, a Doom key and `q` all work, and the terminal is
    restored.

## Interfaces others need

- **The control protocol** the client speaks is mesh-abi's.
  - `snap`, `trace`, `log N`, `programs`, `catalog`, `peek`, `http`, `view fleet|jobs|pipes`, `render <job> raw`, and
    `load` alone get framed replies (u32 LE length + bytes).
  - Every other command gets one text line.
  - A refused framed request answers `err ...` as a text line.
  - meshd must keep these replies as they are.
- **mesh-top-mock** is a viewer fixture.
  - `mt_mock_request(world, line, ...)` answers one request without sockets.
  - `mt_mock_start(addr, host, system)` serves it on TCP.

## Differences from the Rust

- **Layout ties.**
  - ratatui settles exact Cassowary ties differently from process to process. The C settles them one way, every time.
  - Every golden frame was stable across the Rust runs. The tui-layout tests accept any result the Rust produced.
- **Coverage of ratatui and crossterm.**
  - Only what mesh-top uses is ported.
  - Canvas markers: Dot, Block, Bar, Braille and HalfBlock (no Quadrant, Sextant or Octant).
  - Widgets: Block, Paragraph, Table, Sparkline, Chart (as mesh-top uses it: Line and Scatter) and Clear.
  - Keys: tui-input decodes the keys mesh-top binds. Mouse, focus and paste events are consumed and ignored; kitty
    keyboard flags are not decoded.
- **Endpoint lifetime.** `mt_endpoint_free` stops the thread and joins it, interrupting a request in flight. The Rust
  detaches its endpoint threads. The updates queue must outlive the endpoints.
- **Client error texts.** These are Rust's `io::Error` Display texts, because the status line shows them:
  - `Connection refused (os error 111)`, `connection timed out`, `failed to fill whole buffer`.
  - A resolver failure reads `failed to lookup address information: <gai_strerror>`. This matches the Rust on glibc.
- **The HTTP pane** shows the response as a C string, so a response containing a NUL byte is cut there. The Rust shows
  it as U+0000.
- **Time.** The library takes `mt_instant` (ns) everywhere.
  - The apps use the real monotonic clock.
  - The tests use the virtual clock, starting at 1e12 ns.
  - The Rust tests used `Instant::now()`, a few milliseconds apart. No assertion depends on that.
