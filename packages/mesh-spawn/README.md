# @ajax/mesh-spawn

Starting worker processes: `posix_spawn` (never fork: a fork of meshd copies the page tables of its GPU mappings under the mm lock) with this process's environment plus overrides, chosen descriptors dup2'd in (sources moved above the targets first), a process group of its own; close-on-exec pipes; exit statuses as meshd reports them (`exit 3`, `signal 9 (Killed)`) and P_EXIT's encoding.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/native.rs (`spawn`, `pipe`, `describe`, `exit_word`).

## Tests

`tests/spawn.c`: descriptors, environment overrides, the process group, exit codes, signals and a missing program.

## Licence

MIT. See the repository's LICENSE.
