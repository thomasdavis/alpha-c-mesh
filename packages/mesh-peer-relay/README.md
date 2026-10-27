# @ajax/mesh-peer-relay

The fleet thread: N machines run as one fleet with partial membership.

- **Links.** A full mesh of TCP links (`@ajax/mesh-peer-link`). Every member beats every other each
  `ping_ms`. A member is up while its link is up and it was heard within `dead_ms`; it is suspect after
  a third of that. Nothing ends when a member drops: only a lockstep group that contains it pauses.
- **The leader and the desired state** (`@ajax/mesh-fleet-core`).
  - A change asked for on any member (`mpr_change`) goes to the leader as F_CTL, and is answered once
    the reachable members have reconciled it. Without a leader the change is refused.
  - The leader commits the change and waits for the members. If the change failed everywhere, the
    leader reverts it.
  - Each committed document goes to the daemon's reconciler (`committed`).
  - Images and migrated views are fetched from members for the reconciler (`mpr_fetch_image`,
    `mpr_fetch_view`).
- **The leader's policies.**
  - Lockstep groups: pause, resume, and re-form after `auto_ms`.
  - The scheduler (`@ajax/mesh-sched-*`): placement passes, rebalancing, and owner-busy members.
  - Migrations, with the view moved in 1 MiB F_VIEW chunks.
  - All of it works from a model of every member, built from F_JOIN, F_LOADREP and F_PERF (the perf
    board).
- **Lockstep groups.** Members send F_HELLO for each epoch and cycle. Rounds are mirrored point to point
  by a sender thread that writes under each link's writer lock.
- **Published.**
  - The status block's fleet, member and scheduler regions.
  - The `fleet`, `groups` and `sched` texts, the dry run, and the fleet view's records in views.rs's
    text form (`kind k=v ... -- text`).
  - The jobs dispatcher's scheduling model.

Everything daemon-specific comes through `mpr_host`: the status block, the mailbox, the reconciler,
the driver, the catalog, and jobs and blobs. This package holds no daemon state.

## Using it

```c
mpr_host h = {.ctx = d, .host_u32 = ..., .committed = ..., .recon = ..., /* every callback */};
mcc_topology t;
mcc_topology_parse(config_text, me, &t, &err);
mpr_relay *r = mpr_start(&t, &caps, "/var/lib/meshd/fleet-state", &h, &err);
mpr_change(r, "sched cordon 2", (mpr_reply){answered, ctx});
...
mpr_stop(r);
```

Callbacks must be thread-safe. The fleet thread calls most of them, but not all:

- the link reader threads call `place_frame`, `jobs_frame`, `blobs_frame`, F_LOADREP's `host_bytes`,
  and a lockstep round's `mb_write`, `mb_publish64` and `round_stats`, as well as `uptime_ms`;
- the rounds sender thread reads the mailbox;
- a migration's source runs `stop_and_view` on its own thread.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh), `crates/mesh-host/src/`:

- `peer.rs`: `run`, `Relay`, the events, links, beats, changes, the leader, policies, images, the
  lockstep rounds, `publish`, `fleet_text`, `groups_text`, `set_member` and `group_line`.
- `peer_sched.rs`: the member model, `perf_map`, `sched_pass`, `schedule`, migrations, the view
  transfer, `sched_publish` and `fleet_recs`.
- `shared.rs`: `PerfBoard`.

The frame codecs live in `@ajax/mesh-peer-codec` and `@ajax/mesh-place-wire`, and their Rust tests are
ported there.

## Tests

`tests/loopback.c` runs three relays over real loopback TCP, each with a fake daemon (status block,
mailbox, and a reconciler that applies each document at once):

- they elect one leader;
- a change asked for on a follower is forwarded, committed, reconciled everywhere and answered;
- a refused change carries its reason;
- a stopped member is seen gone (fleet core, status block, `fleet` text);
- the majority left keeps committing;
- the member comes back and catches up;
- when the leader stops, the two left elect another;
- with one member left there is no leader, and changes are refused.

## Licence

MIT. See the repository's LICENSE.
