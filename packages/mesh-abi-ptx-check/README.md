# @ajax/mesh-abi-ptx-check (private)

A build check, not a program: a CUDA kernel (C compiled to PTX by clang) that uses the mesh contract
(@ajax/mesh-abi), including a pipe push through `mesh_words` and the tables that live in the packages'
`src/`. Building it compiles, links and code-generates every @ajax/mesh-abi-* package for
`nvptx64-nvidia-cuda` (sm_86 by default); its test checks the module. Never published.
