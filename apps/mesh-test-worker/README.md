# @ajax/mesh-test-worker

The native lifecycle fixture: a mesh worker program whose ops do what lifecycle tests need. Op 0:
`arg * 3 + 7 + 1000 * param` (two images tell apart); OP_INIT: the view magic `NNTW`; 3: sleep
`arg` ms; 4: param; 5: the pid; 9: panic; 10: allocate and touch `arg` MiB (a memory cap kills it);
11: read past the view (a fault); 12: spin `arg` ms; 13: SIGTERM itself if `arg` is 0, else
`exit(arg)`, mid-task. Its setup sleeps `MESH_TEST_SETUP_MS` (a slow model load).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
crates/mesh-worker/src/bin/test_worker.rs.

## Tests

It is the subject of @ajax/mesh-native's lifecycle tests.

## Licence

MIT. See the repository's LICENSE.
