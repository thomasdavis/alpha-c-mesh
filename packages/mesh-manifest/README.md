# @ajax/mesh-manifest

A program's manifest, `program.toml`: the `mesh_manifest` struct, `mesh_manifest_parse` with every key, default and refusal in the Rust's order and with its messages, `mesh_manifest_load` (a dir or its `program.toml`, and the `<name>.ptx` path), the placement parts, and a Debug printer that writes exactly what Rust's derived `{:?}` writes.

```c
#include "mesh/manifest.h"
mesh_manifest m;
char *err = NULL;
if (!mesh_manifest_parse(text, strlen(text), &m, &err)) { puts(err); /* line 3: unknown key "bogus" */ }
```

## Differences

None in behaviour. Type names carry the `mesh_manifest_` prefix (`mesh_manifest_field`, `mesh_manifest_place`, ...) so they never collide with the contract's `mesh_field`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/manifest.rs (`Manifest`, `Field`, `Window`, `Place`, `Inbox`, `parse`, `load`).

## Tests

@ajax/mesh-manifest-encode's golden test: 5696 manifests against the Rust, comparing the verdict, the error text, the `{:?}` of the parsed manifest, the encoded entry and extension, and the place parts.

## Licence

MIT. See the repository's LICENSE.
