# @ajax/mesh-prog-sha256d

Bitcoin-style double SHA-256 nonce grinding as a fleet job program: each chunk of 2^param nonces is a
job item (OP_JOB) ground by a 16-warp node, 2 hashes a thread an iteration on the header's midstate,
the node's best hash by block reductions, and every share recorded by a rescan when any thread saw a
candidate. Configuration under a spin lock and a seqlock; a request window for headers, targets and
status. View layout: `include/mesh/prog/sha256d.h`.

- **Ported from:** alpha-rust-mesh `programs/sha256d/src/lib.rs`.
- **Tests:** on the simulator against @ajax/mesh-ref-sha256d: the genesis header and its midstate, four
  chunks at 8 zero bits (time rolled for one) with every share and the best hash equal to hashing each
  nonce on the CPU, the genesis block's own nonce found as a share with Bitcoin's genesis hash, the
  config ops, the request window (status, errors, a raw header), and a param out of range.
- **Differences:** rotates and byte swaps are plain C (`clang` makes them `shf.r.wrap` and `prmt`);
  rounds 16..48 stay a 2-trip loop (`#pragma clang loop`) as in the Rust.
