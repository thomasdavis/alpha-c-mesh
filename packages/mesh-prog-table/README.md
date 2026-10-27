# @ajax/mesh-prog-table

meshd's writes to the program table: entries and pipe blocks under the PT_GEN seqlock, state changes, inbox holds, flight words, fresh slots, lookups.

The program table as meshd writes it (programs.rs).

meshd is the table's only writer. It rewrites an entry only while that slot's kernel is not running (load, upgrade, rollback, reset happen with it halted), state changes excepted. Rewrites go under the entry's PT_GEN seqlock (odd while writing); every change bumps H_PROG_SEQ so viewers know to re-read. Readers on any thread take a stable copy (read_entry).

```c
#include "mesh/prog_table.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/programs.rs: `write_entry`, `set_state`, `clear`, `write_pipe`, `set_hold`, `flight`, `flight_epoch`, `flight_clear`, `reset_slot`, `name_of`, `find`, `read_entry`.

## Tests

- `tests/prog_table.c`: programs.rs's `reset_slot_leaves_the_gpu_run_request` and `table_seqlock` (a reader waiting out an odd PT_GEN on another thread), and the pipe block, flight words and hold.

## Licence

MIT. See the repository's LICENSE.
