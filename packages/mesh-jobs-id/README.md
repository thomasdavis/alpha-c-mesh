# @ajax/mesh-jobs-id

A job's id and a payload's hash, and their text forms:

- **Job ids** are `t.n`, the n-th job submitted in fleet term t (`mj_job_word`, `mj_parse_job`).
- **Payload hashes** are sha256 values named in lowercase hex (`mj_hex`, `mj_unhex`), with
  `mj_hash_words` to use one as a map key in byte order.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `JobId`, `job_word`, `parse_job`, `Hash`, `hex`, `unhex`.

## Tests

`tests/id.c`: words both ways, what `parse_job` refuses, hex both ways (either case), and the map key
order.

## Licence

MIT. See the repository's LICENSE.
