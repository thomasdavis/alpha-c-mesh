# @ajax/mesh-prog-matmul-check (private)

The CPU-simulator tests of `programs/matmul` (@ajax/mesh-prog-matmul) against @ajax/mesh-ref-matmul,
bitwise: matrices generated on the device, every C tile with dimensions that are not multiples of the
tile, y = A x, both copied out through the view's window, and bad arguments refused.

They live here, not in the program's package, because the reference is host-only (it uses threads)
and sexc has no test-only dependencies: a `ptx` package may only depend on libraries that build for
nvptx64. The test compiles the program's source (`programs/matmul/src/matmul.c`) for the host.
Not published.
