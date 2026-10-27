# @ajax/mesh-buf

A growable byte buffer for building strings and messages: append bytes, text, `printf` output and
little-endian words; the bytes are always NUL-terminated, so a buffer holding text is a C string.
Allocation failure aborts (`mesh_xmalloc` and friends too): meshd never goes on with half a message.

```c
#include "mesh/buf.h"
mesh_buf b = MESH_BUF_INIT;
mesh_buf_printf(&b, "ok %u us", 412u);
char *s = mesh_buf_take(&b); /* free(s) */
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): what Rust's `String`
and `Vec<u8>` (with `format!` and `extend`) do throughout `crates/mesh-host`. There is no single Rust
item; every daemon package builds its replies with it.

## Tests

- `tests/buf.c`: appends, growth, `printf`, little-endian words, take, truncate.

## Licence

MIT. See the repository's LICENSE.
