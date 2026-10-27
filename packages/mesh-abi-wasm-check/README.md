# @ajax/mesh-abi-wasm-check (private)

A build check, not a program: a wasm32 module that uses the mesh contract
(@ajax/mesh-abi), including a pipe push through `mesh_words` and the tables that live in the packages'
`src/`. Building it compiles, links and code-generates every @ajax/mesh-abi-* package for
`wasm32`; its test checks the module. Never published.
