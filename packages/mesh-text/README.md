# @ajax/mesh-text

Growable strings, `printf` into them, error messages (`char **err`), and the pieces of Rust's
formatting that the Rust implementation's messages show: `{:?}` of strings, `f64` and `f32`,
`str::trim` (Unicode whitespace), `u32::from_str` and `f64::from_str`. With them, every ported error
message reads byte for byte as the Rust's.

```c
#include "mesh/text.h"
mesh_text t = {0};
mesh_text_puts(&t, "name ");
mesh_text_debug_str(&t, name, strlen(name));   /* name "Bad" */
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT) uses Rust's own
`format!`; this package reproduces the parts its messages depend on. `src/escapes.c` (the code points
`{:?}` escapes as `\u{..}`) was generated from Rust 1.88's `char::escape_debug`.

## Tests

`tests/text.c`: `{:?}` of 37 f64s, 7 f32s and 16 strings and 5 `trim`s, each against what Rust 1.88
printed; the integer and float parsers' edge cases.

## Licence

MIT. See the repository's LICENSE.
