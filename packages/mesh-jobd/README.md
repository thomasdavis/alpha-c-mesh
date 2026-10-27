# @ajax/mesh-jobd

meshd's jobs thread and blob thread, running over the daemon's shared state (`@ajax/mesh-shared`).

- **The jobs thread** runs this member's job node (`@ajax/mesh-jobs-node`) over four things: the
  peer links (F_JOB_* frames, F_LEASE and F_RESULT), the driver (`MESH_CMD_JOB_LEASE`, `JOB_CUT` and
  `JOB_PROGS`, and `MESH_JEV_DONE` coming back), the job log (`@ajax/mesh-jobd-store`) and the blob
  thread.
  - The node's outputs go through `mj_durable`, so the log is fsynced before any send or answer that
    depends on it. If the log cannot be made durable, the member fail-stops (`mesh_shared_fail_stop`).
  - Table frames (F_JOB_STATE, F_JOB_HAVE) wait up to 2 s for room in a link's queue. Once one has
    waited that long, the rest of the batch to that member is dropped. Other frames are dropped when
    the queue is full.
  - The job programs are asked of the driver every 250 ms and the answer is never waited for. While
    blobd holds more than `MESH_BLOBD_BLOB_MEM_MAX` of this member's payloads in memory, the member
    reports no programs, so it takes no new leases (backpressure).
  - Item payloads are hashed here and handed to blobd before their results go to the node.
  - Every 250 ms it publishes three things: the host block's `S_JOB_*` fields, the jobs view's records
    in `sh->jobs_view_recs`, and blobd's news.
  - It compacts the log when the log asks for it.
- **The control requests.** `jobs`, `job status` and `job results` are answered from this member's
  committed table. Changes (`job submit|cancel|forget|tone`) go to the node when this member leads,
  or when it is a single meshd. Otherwise they are forwarded to the fleet thread (`MESH_PEV_CHANGE`).
  A remote request (F_CTL) is answered with an F_CTL_REPLY frame.
- **The blob thread** runs `@ajax/mesh-blobd` on `sh->blobs`. It has three writer threads, and each
  writer makes a batch of references durable with one directory fsync per batch.

## Wiring

```c
mjd_config jc = {a.jobs_state, a.blobs, (uint64_t)a.blobs_cap_mb << 20, dead_ms /* MESH_PEER_DEAD_MS single */};
mjd *jd = mjd_open(sh, &jc, &err);   /* after mesh_shared_new: replays the log, indexes the store */
if (!jd) fail(err);
mesh_control_render = mjd_render;    /* control's and http's `render` */
...
mjd_start(jd);                       /* instead of mesh_chan_close_rx(sh->jobs) */
```

The fleet thread calls three functions:

- `mjd_set_view(sh, (mj_view){term, has_leader, leader, alive, linked})` whenever its view changes
  (peer.rs).
- `mjd_set_sched(sh, mjs_sched(&cx))` with the scheduler's model, once a second (peer_sched.rs).
- `mjd_route_frame(sh, from, frame, n)` for the link frames of types F_JOB_AVAIL..F_RESULT and
  F_BLOB_PUT..F_BLOB.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):

- `crates/mesh-host/src/jobd.rs`: `run`, `Jobd` (`apply`, `answer`, `ctl`, `publish`, `news`,
  `jobs_recs`).
- `crates/mesh-host/src/blobd.rs`: `run`, `writer`, `render`.
- `crates/mesh-host/src/shared.rs`: `jobs_view`, `jobs_sched`, `render_watch`. In C these live in
  `sh->jobs_ext`.

## Tests

`tests/jobd.c` runs both threads in-process over a `mesh_shared` with a fake driver:

- **Single meshd.** A job with payloads runs to the end. The test checks its leases and results,
  that its payloads are durable in the blob store, the commit, the host block and the jobs view.
- **Control.** The control answers and their errors, and `render` errors.
- **Frame routing.**
- **Backpressure** on payload memory.
- **Restart.** The job log is replayed (a forgotten job stays forgotten) and the blob store is
  indexed.
- **Follower.** A follower in a fleet of 3 forwards a change to the fleet thread and answers a remote
  request with F_CTL_REPLY.

## Licence

MIT. See the repository's LICENSE.
