# @ajax/mesh-inferd

meshd's side of an inference request: which members serve it, the call to their gpt2 worker, the answer.

meshd's side of an inference request (inferd.rs).

The pair: explicit (`prefill=K decode=J`, `mono=K`), or the members where the fleet's desired state placed gpt2-prefill and gpt2-decode (this member's committed copy, read from the `fleet` text), or a monolithic gpt2. Then one call to the decode (or monolithic) member's worker, which answers K_RESULT: the tokens, the text, the logits digest, the timings.

```c
#include "mesh/inferd.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/inferd.rs: `members_of`, `param_of`, `run`.

## Tests

- `tests/inferd.c`: a gpt2 worker on loopback answers a mono request (the message meshd sends, the result it returns with coordinator, request_ms and program), its K_ERROR, a refused connection, and no member placed.

## Licence

MIT. See the repository's LICENSE.
