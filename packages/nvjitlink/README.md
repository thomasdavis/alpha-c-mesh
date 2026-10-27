# @ajax/nvjitlink

nvJitLink, opened at runtime from `--nvjitlink DIR` (`libnvJitLink.so.13`, CUDA 13 only): PTX text to a cubin for the GPU's arch, with `-maxrregcount` when a multi-warp node needs a register cap. One handle per link, destroyed on every path; errors carry nvJitLink's error log. There is no fallback to the driver JIT.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/nvjitlink.rs.

## Tests

`tests/nvjitlink.c`: the Rust's `link_and_refuse` with a fake nvJitLink (the real one needs the CUDA 13 toolkit): the refusal message, the options, NUL-terminated PTX, the error log, and that every handle is destroyed.

## Licence

MIT. See the repository's LICENSE.
