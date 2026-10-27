# @ajax/mesh-catalog-toml

What building the program catalog reads from a `program.toml`, line by line: the `name`, whether it is
a native program (`backends = ["native"]`), whether it has a `backends` line and which backends it
mentions, and the `# hash-also:` and `# build-os:` comments. No TOML parser: the same lines and the
same answers as the Rust build script's `grep` and `sed` (meshd parses manifests properly elsewhere).

- **Ported from:** alpha-rust-mesh `scripts/build-programs.sh` (`is_native`, the `name` and `backends`
  greps, `source_hash`'s and `native`'s comment seds).
- **Tests:** every probe on the manifests' real shapes, including spacing and comments.
