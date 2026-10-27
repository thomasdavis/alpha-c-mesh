# @ajax/mesh-fleetd

meshd's fleet member, wired over the daemon's shared state (`@ajax/mesh-shared`). The fleet area's
`@ajax/mesh-peer-relay` runs the links, the election, the desired state, the leader's policies and the
lockstep data plane, and reaches the daemon only through `mpr_host`. This package is that host, plus
the reconciler's driver actions and the bridges between mesh_shared and the relay.

- **The relay's host** (`mpr_host`):
  - the status block, the mailbox, the lockstep feed and the round statistics are mesh_shared's
    (`host`, `mb`, `lock_feed`, `round_of`, `rtt`, `wait`);
  - the catalog (`catalog`, `image`) is `--programs`, read with `@ajax/mesh-loader`, as peer.rs's
    `catalog` and `serve_image`;
  - the driver: `inventory` (Cmd Inventory), `stop_and_view` (ProgRun off, then ViewGet), `driver_view`
    (FleetView), and F_PLACE / F_FRAME / F_INPUT decoded into Cmd PeerPlace / PeerFrame / PeerInput;
  - jobs and blobs go to `@ajax/mesh-jobd` (`mjd_jobs_frame`, `mjd_blobs_frame`, `mjd_set_view`,
    `mjd_jobs_sched`); a `job ...` request becomes JobEvent::Ctl, answered straight to the control port's
    waiter when the change came through the bridge;
  - the texts and the fleet view's records go where the control port reads them (`fleet_text`,
    `groups_text`, `sched_all`, `sched_dry`, `sched_explain`, `fleet_view`).
- **The reconciler** (`@ajax/mesh-reconcile`'s thread), with reconcile.rs's timeouts (60 s an action,
  35 s an image or a view from a member, 20 s a placement): Load, Upgrade, Unload, Rollback, ProgRun,
  Reset, Task, Place and ViewPut through `mesh_shared_ask`. An image comes from the local catalog when
  it is the committed one (name and hash), else from a member (`mpr_fetch_image`). Its status and
  failures are kept for the beats (`recon`) and the F_PERF (`fails`).
- **The bridges.**
  - The `peer` channel: a change (the control port, the driver's run requests, the jobs thread's
    forwarded `job` changes) becomes `mpr_change`, its answer the waiter's; a wake becomes `mpr_wake`.
    Events wait until the relay runs, as they waited in the Rust's channel.
  - The link queues: every linked member has a `link_out` and a `bulk_out` queue (made and dropped as
    S_LINKED changes, so `mesh_shared_send_peer` refuses a member that is not linked), written to its
    link with `mpr_send`, control frames before each bulk frame. A full link queue is waited on for up
    to 2 s while the member stays linked.
- **F_PERF** (`mesh_load_perf`, load.rs `perf_report`): this member's CPUs, RAM and disk, each loaded
  program's measured performance from the pload records just sampled, the reconciler's failures, the
  perf board's echoes and link RTTs; broadcast and put on the board (`mpr_board_own`).

The fleet thread waits for the driver's startup loads (`sh->ready`) before it starts the relay (the
first leader's document is what it runs). A listener that cannot be bound ends it, as the Rust's thread
returned: the member runs alone and its changes are refused.

## Using it

```c
bool skip_startup;
mesh_fleetd *f = mesh_fleetd_open(sh, &topo, fleet_state, &skip_startup, &err); /* before the driver */
...                                                       /* the driver with skip_startup */
mpk_caps caps = mesh_fleetd_caps(sh, gpu_name, arch);
mesh_fleetd_start(f, &caps);                              /* after the driver started */
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT),
`crates/mesh-host/src/`:

- `main.rs`: the fleet member's start (the store before anything runs, `skip_startup`, the caps).
- `peer.rs`: `run`'s start, `writer`, the reader's direct frames to the driver and jobs, `catalog`,
  `serve_image`, `send_view`; `peer_sched.rs`'s view-out thread.
- `reconcile.rs`: `run`'s driver calls (`source`, `ask`, `inventory`, `fetch_view`, `quiesce`,
  `set_group_words`, `ready`, `status`).
- `load.rs`: `perf_report` and its frame.

## Tests

meshd's `tests/cluster.c` runs three whole meshds on loopback as one fleet (see apps/meshd).

## Licence

MIT. See the repository's LICENSE.
