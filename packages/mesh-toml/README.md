# @ajax/mesh-toml

The value syntax of `program.toml`: integers (decimal or `0x`, `_` separators), negative integers, floats (with a `.`), `"strings"` (no escapes), `true`/`false`, one-line lists of strings, and `#` comments outside strings. Not general TOML: exactly what the Rust's hand-rolled parser accepts, with its error messages (`unterminated string`, `bad value "x"`, ...).

```c
#include "mesh/toml.h"
mesh_toml_val v;
char *err = NULL;
if (!mesh_toml_value("[\"a\", \"b\"]", 10, &v, &err)) ...
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/manifest.rs (`Val`, `value`, `quoted`, `int`, `pair`, `strip_comment`).

## Tests

Through @ajax/mesh-manifest-encode's golden test: 5696 manifests (every `program.toml` of the Rust repo, the Rust's test cases and mutations of every key) give the Rust's verdicts byte for byte.

## Licence

MIT. See the repository's LICENSE.
