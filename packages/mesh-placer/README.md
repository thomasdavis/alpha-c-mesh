# @ajax/mesh-placer

One placed program's state machine across the cluster (`mpl_placer`), sans-IO. The driver feeds it
events (a frame batch completed here, a peer frame arrived, a placement request, the fleet's view, a
tick) and carries out the acts it returns (send a frame, write the state into the view, show a composed
frame, publish the follow batch, change the run state, answer a request).

The fleet's leader coordinates every change among the up members: it pauses the others (PL_PAUSE),
collects their drained state and time per part (PL_PAUSED), picks the state that continues (the old
mode's authority's, or a survivor's copy when it is gone), sets the new mode everywhere (PL_SET with
that state) and waits for PL_DONE; a change not done in 5 s is rolled back (PL_FAIL). It re-plans at once
when a member of the mode drops, and 5 s after one comes back; with no leader, a mode that needs a lost
member goes local. Frames go point to point: a runner sends whole frames, a split owner sends its parts
and the state, followers render their parts on the owner's state and send them on, and every machine
(mirrors included) composes and shows the same whole frame.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/place.rs`:
`Placer`, `Act`, `Dest`, `Stats`, `CHANGE_TIMEOUT`, `JOIN_DWELL`. Instants are monotonic microseconds.

## Tests

`tests/placer.c`: the Rust's `change_protocol_moves_state_across_three`,
`split_auto_uses_measured_times`, `change_times_out_and_rolls_back`,
`split_frames_assemble_on_every_machine` (six splits over 2..4 machines),
`follower_keeps_the_newest_and_rejects_strays` and `member_loss_replans_and_return_rejoins`, with the
Rust's message pump between placers.

## Licence

MIT. See the repository's LICENSE.
