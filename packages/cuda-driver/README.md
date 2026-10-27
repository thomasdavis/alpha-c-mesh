# @ajax/cuda-driver

The CUDA driver API calls meshd needs, as a table of functions loaded from `libcuda.so.1` at runtime: building needs no CUDA headers or toolkit. The types and constants are the CUDA 13.0 ABI as alpha-rust-mesh's cudarc 0.19 bindings declare them (including `CUdevResource` for green contexts). `mesh_cu_ck` gives cuda.rs's error messages (`cuInit: CUDA_ERROR_NO_DEVICE`); `src/names.c` holds every CUresult name. Tests swap the table (`mesh_cuda_set`), as @ajax/cuda-driver-fake does.

## Differences

The Rust's cudarc panics when it cannot load the driver; here `mesh_cuda_load` returns an error naming `libcuda.so.1`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/cuda.rs (its `ck`, and the cudarc calls it makes).

## Tests

`tests/driver.c`: names, both error styles, an injected table, and loading on a machine without the driver.

## Licence

MIT. See the repository's LICENSE.
