# @ajax/mesh-jobs-req

A change of the job table, as the control port's words (the words after `job`):

```
job submit <program>[,<program>..] items <n | a-b | a- | a,b-c,..> [op <o>] [priority <p>]
           [attempts <a>] [deadline <secs>] [results collect|nonzero|none] [setup <op>:<arg>,..]
           [members <k>,..]
job cancel <term>.<n>
job forget <term>.<n>
job tone <term>.<n> aces|reinhard
```

Parsing follows the Rust's rules exactly. `items 9` means 0..9. `op` and `setup` take `0x` hex. An
endless job can't collect per-item results. There are at most 16 setup tasks. Every error message is
the Rust's, word for word.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Submit`, `SUBMIT_USAGE`, `int`, `Req` (`parse`).

## Tests

`tests/req.c`: 36 control lines, valid and not, each answered as the Rust answers it (golden answers
from the Rust), including the requests of the Rust's `ops_frames_and_the_merge` and
`a_job_of_two_programs`.

## Licence

MIT. See the repository's LICENSE.
