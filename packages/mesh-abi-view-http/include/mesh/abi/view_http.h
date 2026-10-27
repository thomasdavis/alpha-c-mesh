/* @ajax/mesh-abi-view-http: programs/http's view: a GPU HTTP server's counters, load request and windows.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_VIEW_HTTP_H
#define MESH_ABI_VIEW_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* programs/http: an HTTP/1.0 GET server computed on the GPU, generic over the
 * program table (`reads = ["*"]`).  Request window "0x100:0x100,0x400:0x4000".
 *
 * The load request (`GET /load/<name>`): when HV_LOAD_SEQ == HV_LOAD_ACK
 * (none pending) the warp writes the name (NUL padded) at HV_LOAD_NAME, then
 * HV_LOAD_SEQ += 1 (release); otherwise it answers 503.  meshd, seeing
 * HV_LOAD_SEQ != HV_LOAD_ACK in the slot of the program named "http", loads
 * `<--programs DIR>/<name>` (the same checks as the control `load`), writes
 * HV_LOAD_RESULT (LOAD_OK or 1 + an index into LOAD_REASONS), then
 * HV_LOAD_ACK = HV_LOAD_SEQ (release). */
#define MESH_HTTP_HV_SERVED 0x00u /* u32 responses written */
#define MESH_HTTP_HV_LAST_CODE 0x04u /* u32 the last status code */
#define MESH_HTTP_HV_LOAD_SEQ 0x08u /* u32 GPU */
#define MESH_HTTP_HV_LOAD_ACK 0x0cu /* u32 meshd */
#define MESH_HTTP_HV_LOAD_RESULT 0x10u /* u32 meshd */
#define MESH_HTTP_HV_LOAD_NAME 0x14u /* [u8; 16] GPU */
/* Per slot {P_DONE u32, pad, globaltimer u64} at the last page that showed
 * rates: tasks/s between two requests. */
#define MESH_HTTP_HV_SAMPLES 0x200u
#define MESH_HTTP_SAMPLE_BYTES 16u
#define MESH_HTTP_REQ_OFF 0x100u
#define MESH_HTTP_REQ_MAX 0x100u
#define MESH_HTTP_RESP_OFF 0x400u
/* Room for /render.bmp of a 640x360 pathtrace image (54 + 691200 bytes) and its head. */
#define MESH_HTTP_RESP_MAX 0xa9000u
#define MESH_HTTP_VIEW_BYTES 0xa9400u
#define MESH_HTTP_LOAD_OK 0u
/* /doom: rows of the ASCII frame. */
#define MESH_HTTP_DOOM_ROWS 24u

#endif
