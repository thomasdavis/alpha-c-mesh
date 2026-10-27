# @ajax/mesh-sched-place

The fleet scheduler's placement pass (`msp_place`): Nomad's feasibility then score, program by program in
a fixed order (priority, anchors before what is colocated with them, name; ties go to the lower system
id), so every leader computes the same answer from the same inputs. A valid placement is kept: a pass
only fills what is missing (a new program, a replica on a member down past the grace, a member that no
longer fits); pins, colocation, lockstep programs and migrations in flight are honoured. Reservations
(CPU, memory, the view heap, free slots) are admission. Each program gets an explain
(`msp_explain_text`: every member's verdict, chosen / feasible with its score terms / rejected with the
reason). `msp_group_choice` picks a lockstep group's members (best scores, grown for link RTT).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/sched.rs`: `Input`,
`Explain` (`text`, `score_of`), `order`, `members_of_at`, `Books`, `rows`, `sorted_ok`, `place`,
`group_choice`, `group_score`, `list`, `at_word`.

## Tests

`tests/place.c`: the Rust's `placement_respects_constraints`, `same_inputs_same_decisions`,
`reservations_are_admission`, `a_member_down_is_replaced_after_the_grace` and the placement half of
`node_shape_filters_members`. The rebalancer's tests (`@ajax/mesh-sched-tracker`) drive `place` too.

## Licence

MIT. See the repository's LICENSE.
