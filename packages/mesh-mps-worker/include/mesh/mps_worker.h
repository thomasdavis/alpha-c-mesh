/* @ajax/mesh-mps-worker: `meshd worker` (--isolation mps; mesh-host
 * mps_worker.rs): one program image in its own MPS client process.  It maps
 * meshd's mailbox file, registers it with DEVICEMAP (its own device address),
 * loads its one cubin, and launches mesh_main when meshd says so; a monitor
 * thread reports when the stream finished.  Frames on stdin/stdout
 * (@ajax/mesh-mps-wire); logs on stderr.  It exits on W_QUIT or on EOF (meshd
 * gone, however it died: then it halts its own kernel first). */
#ifndef MESH_MPS_WORKER_H
#define MESH_MPS_WORKER_H

/* argv: [prog, "worker"?,] "--mailbox", FILE [, "--sms", N].  Never returns. */
_Noreturn void mesh_mps_worker_main(int argc, char **argv);

#endif
