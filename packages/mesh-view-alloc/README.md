# @ajax/mesh-view-alloc

The view heap's first-fit page allocator (meshd gives each loaded program its view from it).

First-fit allocation over the view heap's VIEW_PAGES pages (programs.rs Heap). A view of 0 bytes takes no page (offset 0). Offsets are mailbox offsets. Freeing a free page is a bug in meshd: it aborts, as the Rust panics.

```c
#include "mesh/view_alloc.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/programs.rs: `Heap` (alloc, free, free_pages, largest_run), `pages`.

## Tests

- `tests/view_alloc.c`: programs.rs's `heap_first_fit` and `heap_double_free` (the abort is checked in a child process).

## Licence

MIT. See the repository's LICENSE.
