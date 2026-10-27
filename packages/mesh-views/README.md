# @ajax/mesh-views

The fleet's view records (kind key=value -- text) as text and JSON, and their parser.

The fleet's views (docs/plan-fleet-scheduling.md F6) as records.

`/fleet`, `/jobs` and `/pipes` on the http port, `view fleet|jobs|pipes [json]` on the control port, and mesh-top's tabs all read one producer per view as records:

member id=2 state=up leader=0 host=being gpu=cuda share=0.48 -- busy: cpu psi 0.31

A record is a kind, `key=value` fields (a value has no spaces or `=`: each becomes `_`; an empty value is `-`, "none") and an optional free text after ` -- `. The text form is one record a line; the JSON form groups records by kind in first-seen order, every record an object of its fields plus "text": `-` is null, a field named in MESH_VIEWS_STRING_KEYS is a string, any other that is a JSON number is a number.

```c
#include "mesh/views.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/views.rs: `Rec` (new, kv, opt, f, text, get), `text`, `json`, `json_number`, `STRING_KEYS`, `parse`, `format_of`, `render`.

## Tests

- `tests/views.c`: views.rs's `records_round_trip_and_json` one for one (text form, parse back, JSON with nulls, numbers and string keys, `format_of`), plus Unicode spaces and `\r\n` lines.

## Licence

MIT. See the repository's LICENSE.
