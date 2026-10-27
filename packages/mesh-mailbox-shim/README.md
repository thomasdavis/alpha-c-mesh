# @ajax/mesh-mailbox-shim

PRIVATE and temporary: a stand-in for the exec area's mailbox package (mesh-host `mailbox.rs`), so the
daemon packages compile and test before it lands. It mirrors the Rust `Mailbox` one for one (`get`,
`set`, `publish`, `fetch_add`, `cas`, the 64-bit forms, `read`, `write`, `zero`, the pipe words, a test
mailbox). It is never published; the daemon switches to the real package and this is deleted.

## Licence

MIT. See the repository's LICENSE.
