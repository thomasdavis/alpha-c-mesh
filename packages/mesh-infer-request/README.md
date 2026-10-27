# @ajax/mesh-infer-request

An inference request: the control port's infer words and GET /infer's query, parsed; results as text.

```
@ajax/mesh-infer-request: an `infer` request (infer.rs Request, parse,
from_query, text): `infer [fast|fast16|exact|exact-cpu] [max N]
[prefill=K decode=J | mono=K] [repeat=R] [len=N] <prompt>` on the control
port, `GET /infer?prompt=..&max=..&mode=..` on meshd's http.
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/infer.rs: `Request`, `USAGE`, `parse`, `text`, `from_query`, `url_decode`.

## Tests

- `tests/request.c`: infer.rs's `requests_parse`, and the error texts.

## Licence

MIT. See the repository's LICENSE.
