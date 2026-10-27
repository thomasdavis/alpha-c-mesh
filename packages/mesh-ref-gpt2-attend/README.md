# @ajax/mesh-ref-gpt2-attend

One GPT-2 head's attention for one query, in the GPU node's order: thread t scores keys t, t + 256, …
(each a sequential 64-term dot from +0, times 1/8), the max and the sum of exponentials by block trees,
p_j = e_j / sum, and output lane d sums p_j·v_j[d] in four groups (j = g, g + 4, …) added in order. Keys
and values are addressed by base pointer and stride, so the KV cache is read in place.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`attend_row`). Freestanding (the scores live on the
stack, at most 1024 keys). Compiled with `-O2` in every profile.

**Tested** by gpt2.rs's `attention_of_one_key_is_its_value`, and golden data over 1 to 600 keys in
`@ajax/mesh-ref-gpt2`'s tests.

## Differences

- Rust takes closures returning each key and value slice; here a pointer and a stride. More than 1024
  keys returns false instead of growing a `Vec`.
