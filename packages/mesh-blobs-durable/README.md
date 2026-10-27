# @ajax/mesh-blobs-durable

Blob files made durable. The bytes are written to `name.tmp`, fsynced and renamed, so a blob is
either whole under its name or absent (a crash leaves at most a `.tmp`). The rename is durable once
the directory is fsynced, and the writers batch that. A job's copy of bytes another job already
holds is a hard link of that file, so the bytes are on disk once. If the linked file went meanwhile
(its job forgotten), the bytes are written instead. This uses fsync(2), not a drive-cache flush
(macOS's `F_FULLFSYNC`), which is the job log's guarantee too.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/blobs.rs`: `make_durable`, `write_durable`, `sync_dir`, `fsync`.

## Tests

`tests/durable.c`: whole files with no `.tmp` left, links sharing an inode, relinking over a name, a
vanished link source written instead, a failing directory sync, `mb_remove_tree`.

## Licence

MIT. See the repository's LICENSE.
