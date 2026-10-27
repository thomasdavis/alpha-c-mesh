# @ajax/mesh-jobs-ranges

A set of u64 items as disjoint, non-touching half-open ranges `[s, e)`, in order: a job's items (a
Slurm-like array, or endless: `[s, UINT64_MAX)`). Insert, remove, union, subtract, intersect, take
from the front, the n-th item, and the text form `a-b,c-d` (`a-` is endless, `-` is empty) with its
parser (which also takes single items `a`).

```c
#include "mesh/jobs/ranges.h"
mj_ranges r = mj_ranges_of(0, 100);
mj_ranges_take(&r, 7);            /* 0-7,8-100 */
mj_ranges front = mj_ranges_take_front(&r, 10);
```

A zeroed `mj_ranges` is the empty set; free with `mj_ranges_free`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`, `Ranges` (a `BTreeMap<u64, u64>` there; a sorted array of pairs here,
with the same results).

## Tests

`tests/ranges.c`: jobs.rs's `ranges` test, the parse error text, and 20 000 random inserts, removes,
intersections, `nth` and `contains` against a bitset, with the word form parsed back.

## Licence

MIT. See the repository's LICENSE.
