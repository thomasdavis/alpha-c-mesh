# @ajax/mesh-jobd-text

The jobs control answers, read from this member's committed table, and on the leader also its
leases, rates and lease choices:

- `jobs`: this member's role in the job table, and one line per job (state, items done, failed,
  leased, queued, dupes, rate);
- `job status <id>`: its line, its spec, who ran how many, its leases, the lease choice per member,
  the cancel reason, the first 50 items failed;
- `job results <id> [from] [n]`: the results (item, result, lease, member, bytes, payload
  reference).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobd.rs`: `jobs_text`, `job_line`, `status_text`, `results_text`.

## Tests

`tests/text.c`: a single meshd's node runs a job, and every answer's lines are checked: the header,
job lines (running and done), status (spec, members, picks, failures), and results paged.

## Licence

MIT. See the repository's LICENSE.
