# @ajax/mesh-log

meshd's log: `mesh_info` / `mesh_error` lines with a UTC `HH:MM:SS.mmm` stamp, written to stderr by a log thread so no caller (the driver thread included) ever waits on stderr. A bounded queue of 4096 lines: when it is full the line is dropped and counted (`mesh_log_dropped`, the status block's S_LOG_DROPPED). The last 1000 lines are kept for meshd's `log` request. `mesh_log_exit` flushes for at most 500 ms.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/log.rs.

## Tests

`tests/log.c`: the line format, the recent ring, and a reader-less pipe: emitting still returns at once and the drops are counted.

## Licence

MIT. See the repository's LICENSE.
