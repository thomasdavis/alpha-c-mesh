# @ajax/meshd

meshd: the host daemon of the GPU mesh: the executors, the driver, the control and HTTP ports, the samplers.

meshd: the host daemon of the GPU mesh: the executors, the driver, the control and HTTP ports, the samplers.

```
meshd --backend wasm --programs out/programs --autoload
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/main.rs: `main`, `install_signals`, `watchdog`, `SHUTDOWN_LIMIT`, `hostname`, `bind`, `open_cuda`.

## Tests

- `tests/meshd.c`: a whole CPU-only meshd (`--backend wasm`) in a child process, on a catalog with the real programs/hello-wasm and a native worker (apps/mesh-test-worker): autoload, tasks, stop/start, reset, unload/load, peek, `programs`, the views, a native crash failing only that program, `--state`, SIGINT and exit 0; the CUDA backend without a GPU exits 1, a bad flag 2.

## Differences

- **Not wired yet**: the fleet thread (peer.rs `run`), the jobs thread (jobd.rs `run`) and the blob thread (blobd.rs `run`): `--cluster`/`--peer` is refused with that reason, `jobs` answers "the jobs thread is gone", `render` "the blob thread is gone".
- The executors line names the C wasm interpreter where the Rust names wasmtime.

## Licence

MIT. See the repository's LICENSE.
