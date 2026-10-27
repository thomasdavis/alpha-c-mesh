# @ajax/mesh-prog-doom

A DDA raycaster, one lane a screen column, 4 tasks a frame. The frame's arg-0 task drains the input
queue (w s a d q e) into the pose with wall collision and releases it; the frame's other tasks wait for
it, bounded. Split across machines (`place = "split"`), followers render with OP_FOLLOW at the pose meshd
wrote. Layout: @ajax/mesh-abi-view-doom.

- **Ported from:** alpha-rust-mesh `programs/doom/src/lib.rs`.
- **Tests:** kcheck's checks 3 and 3b (and 3c's follow) on the simulator, against @ajax/mesh-ref-doom
  bitwise: the map and start pose after OP_INIT, seven poses' frames (kind, side, distance and height of
  every column), 75 keys through the input queue with the pose bitwise equal to the reference's every
  frame, a follow that moves nothing, and no stalls.
