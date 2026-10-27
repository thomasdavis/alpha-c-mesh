# @ajax/mesh-omap

An ordered map from fixed-width u64 keys (1 to 6 words, compared word by word) to fixed-size values:
an AVL tree. It is the C port's stand-in for Rust's `BTreeMap` wherever order matters: iteration in
key order, the first and last entries, and range queries (`floor`, `ceil`, `next`, `prev`,
`split_off`).

```c
#include "mesh/omap.h"
momap *m = momap_new(1, sizeof(uint64_t), NULL, NULL);
*(uint64_t *)momap_put1(m, 7, NULL) = 42;
uint64_t k;
for (uint64_t *v = momap_first(m, &k); v; v = momap_next(m, &k, &k)) { ... }
```

Values live in the nodes; a pointer to one is valid until the map changes. Iterating by key (as
above) stays correct when the loop body removes or adds entries. A value that owns memory gets a
release function (and a deep-copy function for `momap_clone`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT) uses
`std::collections::BTreeMap` throughout `crates/mesh-host`; this is the C port's equivalent, written
for the jobs and blobs packages (`jobs.rs`, `blobs.rs`, `blobd.rs`).

## Tests

`tests/omap.c`: random puts, deletes and takes against a sorted-array reference with one- and
three-word keys, every navigation query, deep clones, `split_off`, and a count of owned values (each
released exactly once).

## Licence

MIT. See the repository's LICENSE.
