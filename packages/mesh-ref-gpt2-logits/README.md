# @ajax/mesh-ref-gpt2-logits

What is done with GPT-2's logits: the greedy pick (the first index of the largest logit, by the FTZ
compare) and `digest_extend`, FNV-1a 64 over the logits' bits continued from the previous digest, so one
digest covers every step of a generation.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`argmax`, `digest_extend`, `DIGEST_SEED`). Freestanding.
**Tested** in `@ajax/mesh-ref-gpt2`'s golden data.
