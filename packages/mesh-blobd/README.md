# @ajax/mesh-blobd

The blob thread's logic, sans threads: this member's blob store (`@ajax/mesh-blobs-store`), the blob
frames on the peer links (`@ajax/mesh-blobs-frame`), and the image assemblies
(`@ajax/mesh-assemble`).

- **Own payloads.** The jobs thread hands over this member's payloads. Each is reserved in the store
  and written by the writers (`.tmp`, fsync, rename, then one directory fsync per batch). It is kept
  in memory until its copies are durable, and those bytes are counted so jobd can stop taking leases
  past `BLOB_MEM_MAX`.
- **Pushes.** `F_BLOB_PUT` goes through the link's bulk queue, with at most `PUT_WINDOW`
  unacknowledged per member. `F_BLOB_ACK` comes back once the receiver's copy is durable, or refused
  when its store is full.
- **Every copy is a job's.** A push, an own payload or a kept fetch makes that job's reference, even
  when the same bytes are already here for another job.
- **Fetches.** These serve the leader's parked results and an assembly's next passes. Each blob is
  asked of one holder at a time (else any member up), and the next is tried on a refusal, a timeout or
  a blob that doesn't hash to its name. At most `FETCH_MAX` are in flight.
- **Assemblies.** These cover the jobs a viewer asked for (`render`), and on the leader every active
  pathtrace job. Each tile's passes are read here or fetched, and folded in pass order. `render`
  answers with the status text, a PPM, a BMP, or the raw tiles for mesh-top.

The world is an `mbd_io`: the peer links, jobd, the writers, the clock. `@ajax/mesh-blobd-run` runs
it as meshd's blob thread.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/blobd.rs`: the constants, `JobInfo`, `News`, `Why`, `Fetch`, `Asm`, `Blobd`
(`keep`, `ensure`, `event`, `ack`, `frame`, `news`, `watch`, `tick`, `render`, `resolve`), `fold`,
and the writers' batch.

## Differences

- The Rust's fetches are a `HashMap`, so their order is arbitrary. Here they go in hash order.
- In `render raw` the words are written in host byte order, which is little-endian on every member
  the mesh runs on.

## Tests

`tests/blobd.c`: three blobd's over a fake network:
- an own payload kept, counted, pushed once and acknowledged;
- a full store refusing a push;
- a fetch from a holder;
- a corrupt push dropped;
- a forget removing references, and a push for a forgotten job refused;
- on-disk writes through the writers;
- `render` (status, ppm, an unknown job) of an image assembled from a committed result.

## Licence

MIT. See the repository's LICENSE.
