# @ajax/mesh-jobs-spec

What a job is: a program (or several joined by commas, one item set leased to members running any of
them), an op, its items, a priority, how many tries an item gets, a deadline, a result mode
(`collect`, `nonzero`, `none`), setup tasks run before a member's first item, when it was submitted
and which members may run it. Its one-line text form is how a Submit op carries it:

```
id 4294967297 program p op 254 items 0-10 priority 0 attempts 3 deadline 0 mode collect setup 6:0 submitted 5 members 0x5
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Mode`, `JobSpec` (`endless`, `programs`, `runs`, `encode`,
`decode`).

## Tests

`tests/spec.c`: the text form both ways (as `ops_frames_and_the_merge` starts), several programs,
endless items, every whitespace, and decode's refusals with the Rust's messages.

## Licence

MIT. See the repository's LICENSE.
