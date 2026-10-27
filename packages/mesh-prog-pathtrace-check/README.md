# @ajax/mesh-prog-pathtrace-check (private)

The CPU-simulator tests of `programs/pathtrace` (@ajax/mesh-prog-pathtrace) against
@ajax/mesh-ref-pathtrace (the renderer in the cluster float mode Z), bitwise, as kcheck's PT checks: a
scene uploaded through the request window, every item as an OP_JOB in lease order with each tile's
accumulators, pixels and digests equal to the reference's, a re-run that folds nothing, OP_TONEMAP's
re-toned pixels, OP_PASS's payload digest, pathtrace-live's OP_TASK items, and bad scenes refused.

They live here because the reference is host-only (threads, malloc) and sexc has no test-only
dependencies. Not published.
