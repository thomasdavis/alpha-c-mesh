# @ajax/mesh-exec-wasm-testkit

Runs wasm guest programs through the real wasm executor (`@ajax/mesh-exec-wasm`) on a test mailbox, with
the driver's part written out minimally: a catalog dir (program.toml and `<name>.wasm`), the program-table
entry (name, state, a view in the view heap, nodes, peer mode, `reads`, windows, param, a pipe inbox), the
launch, tickets published on the slot's ring, requests, pipe connections, and the shutdown. For tests; it
is not meshd. `mesh_kit_artifact` finds a built guest module beside the running test binary.
