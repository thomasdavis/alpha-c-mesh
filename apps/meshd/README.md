# @ajax/meshd

meshd: the host daemon of the GPU mesh: the executors, the driver, the control and HTTP ports, the
samplers, the jobs and blob threads (@ajax/mesh-jobd), and in a fleet the member's links, leader and
reconciler (@ajax/mesh-fleetd over @ajax/mesh-peer-relay).

```
meshd --backend wasm --programs out/programs --autoload
meshd --backend wasm --programs out/programs --cluster cluster.conf --system 1 --fleet-state /var/lib/meshd/fleet
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/main.rs: `main` (the fleet state before anything runs, the job log and the blob store, the jobs, blob and fleet threads), `install_signals`, `watchdog`, `SHUTDOWN_LIMIT`, `hostname`, `bind`, `open_cuda`.

## Tests

- `tests/meshd.c`: a whole CPU-only meshd (`--backend wasm`) in a child process, on a catalog with the real programs/hello-wasm and a native worker (apps/mesh-test-worker): autoload, tasks, stop/start, reset, unload/load, peek, `programs`, the views, a native crash failing only that program, `--state`, SIGINT and exit 0; the CUDA backend without a GPU exits 1, a bad flag 2.

- `tests/cluster.c`: three whole meshds (`--backend wasm`) on loopback as one fleet: one leader agreed by all, `load`/`start` asked of a follower committed and reconciled everywhere, a 240-item hashjob array through a follower with every item exactly once and every result recomputed (mesh-ref sha256d), `jobs` on every member, the HTTP relay, a member killed (SIGKILL) seen down while the two left keep their leader and commit, exit 0 on SIGTERM.

## Differences

- The executors line names the C wasm interpreter where the Rust names wasmtime.

## Licence

MIT. See the repository's LICENSE.
