/* @ajax/mesh-abi-host-load: This machine's load (S_LOAD..): GPU share, CPU, PSI, memory, meshd, disk, thermal, cores.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_LOAD_H
#define MESH_ABI_HOST_LOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- load (snapshot v3) */
/* Live load, sampled by meshd about once a second (timed by S_LOAD_AT_MS): */
/* how much GPU its work obtained (from the node load records NL_*), the */
/* host's CPU, memory and disk pressure, meshd's own cost, every member's */
/* summary (F_LOADREP), and per program the GPU share, task latency and */
/* throughput the scheduler will use.  0x1000..0x2800, mirrored into the */
/* mailbox's LOAD_PAGE.  Rates and fractions are f32 over the last sample */
/* interval ("1 s") or the last LOAD_WINDOW samples ("10 s").  A value whose */
/* LF_* flag is clear was not measured (not 0). */
/* 0x1000..0x1100  this machine (S_LOAD_*, S_GPU_*, S_CPU_*, S_PSI_*, S_MEM_*, S_MESHD_*, ...) */
/* 0x1100..0x1200  per logical CPU: utilisation and core class (S_CORE_UTIL, S_CORE_CLASS) */
/* 0x1200..0x1400  per member: its F_LOADREP summary (ML_*) */
/* 0x1400..0x1800  the fleet: leader, term, versions, the view, per-member reconcile, the group (S_FLEET..) */
/* 0x1800..0x2800  per program slot: PL_* (0x00..0x40 GPU and task stats; 0x40..0x80 */
/* PL_HOST: reserved for the CPU executors' per-program CPU time and RSS) */
/* 0x2800..0x3000  reserved */
#define MESH_S_LOAD 0x1000
/* The end of the mirrored load regions (S_LOAD..S_LOAD_END -> LOAD_PAGE). */
#define MESH_S_LOAD_END 0x2800
/* Samples the "10 s" figures span. */
#define MESH_LOAD_WINDOW 10
#define MESH_S_LOAD_AT_MS 0x1000 /* u64 uptime ms of the last sample (0: none yet) */
#define MESH_S_LOAD_SAMPLES 0x1008 /* u32 samples taken */
#define MESH_S_LOAD_FLAGS 0x100c /* u32 LF_*: what this machine measures */
/* f32 the fraction of wall time meshd's GPU work could run (1 - lost / observed
 * over every node of every program, NL_*), last second and last LOAD_WINDOW. */
#define MESH_S_GPU_SHARE 0x1010
#define MESH_S_GPU_SHARE10 0x1014
/* f32 (10 s): the mean run on the GPU between two gaps (the time slice we get), us. */
#define MESH_S_GPU_SLICE_US 0x1018
/* f32 (10 s): the mean gap, us (CUDA: a time slice someone else got; Metal: per command buffer). */
#define MESH_S_GPU_GAP_US 0x101c
/* f32 the longest gap of the last window (1 s), us: the worst stall a task saw. */
#define MESH_S_GPU_MAX_GAP_US 0x1020
#define MESH_S_GPU_GAPS_S 0x1024 /* f32 gaps per second (1 s) */
/* f32 node-seconds observed per second (1 s): the evidence behind the share
 * (0: no idle node, no lease command buffer: nothing observed, LF_GPU clear). */
#define MESH_S_GPU_OBS 0x1028
/* f32 the OS's own device utilisation, 0..1 (Metal: IORegistry AGXAccelerator
 * "Device Utilization %"; LF_GPU_EXT).  Informational: busy is not ours. */
#define MESH_S_GPU_UTIL_EXT 0x102c
#define MESH_S_GPU_QUEUE_US 0x1030 /* f32 (10 s) Metal: mean commit -> GPU start per command buffer */
/* f32 (10 s): wall time per unit of GPU time our work got (1 / share): the latency inflation. */
#define MESH_S_GPU_INFLATE 0x1034
/* f32 0..1 (1 s): busy fraction of every logical CPU, and of the big and little classes. */
#define MESH_S_CPU_UTIL 0x1038
#define MESH_S_CPU_BIG 0x103c
#define MESH_S_CPU_LITTLE 0x1040
#define MESH_S_CPU_IOWAIT 0x1044 /* f32 0..1 (1 s) Linux iowait */
#define MESH_S_LOADAVG 0x1048 /* f32 x 3: 1, 5, 15 minutes */
#define MESH_S_CPUS 0x1054 /* u32 logical CPUs sampled (<= MAX_CPUS) */
#define MESH_S_CPUS_BIG 0x1058 /* u32 */
#define MESH_S_CPUS_LITTLE 0x105c /* u32 (0: one core class) */
/* f32 0..1: Linux PSI, the share of the last interval some (full) task
 * stalled on CPU, memory, IO, from the `total` counters; and the kernel's
 * avg10 (LF_PSI). */
#define MESH_S_PSI_CPU 0x1060
#define MESH_S_PSI_MEM 0x1064
#define MESH_S_PSI_MEM_FULL 0x1068
#define MESH_S_PSI_IO 0x106c
#define MESH_S_PSI_IO_FULL 0x1070
#define MESH_S_PSI_CPU10 0x1074
#define MESH_S_PSI_MEM10 0x1078
#define MESH_S_PSI_IO10 0x107c
#define MESH_S_MEM_TOTAL_MB 0x1080 /* u32 */
/* u32 Linux MemAvailable; macOS free + inactive + speculative pages. */
#define MESH_S_MEM_AVAIL_MB 0x1084
#define MESH_S_SWAP_TOTAL_MB 0x1088 /* u32 */
#define MESH_S_SWAP_USED_MB 0x108c /* u32 */
#define MESH_S_MEM_COMPRESSED_MB 0x1090 /* u32 macOS: the compressor's pages */
/* u32 macOS kern.memorystatus_vm_pressure_level: 1 normal, 2 warn, 4 critical (LF_MEM_LEVEL). */
#define MESH_S_MEM_LEVEL 0x1094
/* f32 0..1 memory free: macOS kern.memorystatus_level (memory_pressure's
 * "free percentage"); Linux MemAvailable / MemTotal. */
#define MESH_S_MEM_FREE 0x1098
#define MESH_S_SWAP_OUT_S 0x109c /* f32 pages swapped out per second (1 s) */
/* f32 meshd's CPU seconds per second (1.27: 127 %; its lease lanes included). */
#define MESH_S_MESHD_CPU 0x10a0
#define MESH_S_MESHD_RSS_MB 0x10a4 /* f32 */
#define MESH_S_MESHD_THREADS 0x10a8 /* u32 */
/* u32 free and total MB of the file system holding --programs (the catalog). */
#define MESH_S_DISK_FREE_MB 0x10ac
#define MESH_S_DISK_TOTAL_MB 0x10b0
/* u32 macOS NSProcessInfo.thermalState: 0 nominal, 1 fair, 2 serious, 3 critical (LF_THERMAL). */
#define MESH_S_THERMAL 0x10b4
#define MESH_S_TEMP_C 0x10b8 /* f32 Linux: the hottest thermal zone (LF_TEMP) */
#define MESH_S_SAMPLE_US 0x10bc /* u32 the last sample's own cost, us */
/* The CPU executors, machine-wide (the sums of PL_HOST over the slots, and the native workers' counts): */
#define MESH_S_EXEC_CPU 0x10c0 /* f32 (1 s) CPU seconds per second of every wasm lane and native worker */
#define MESH_S_WASM_LANES 0x10c4 /* u32 wasm lanes running */
#define MESH_S_EXEC_RSS 0x10c8 /* u64 their resident bytes */
#define MESH_S_WORKERS 0x10d0 /* u32 live native worker processes (gated and running) */
#define MESH_S_WORKER_SPAWNS 0x10d4 /* u32 processes started since meshd started */
#define MESH_S_WORKER_CRASHES 0x10d8 /* u32 of those, died while running */
/* u32 meshd's log lines not written to stderr (its bounded queue was full: a
 * stderr nobody reads; or the write failed).  Kept in the `log` request's memory. */
#define MESH_S_LOG_DROPPED 0x10dc
#define MESH_THERMAL_STATES_LEN 4
extern const char *const MESH_THERMAL_STATES[4];
#define MESH_LF_GPU 1u /* S_GPU_SHARE* observed (some node idled / some command buffer ran) */
#define MESH_LF_PSI 2u /* S_PSI_* */
#define MESH_LF_CLASSES 4u /* big/little core classes known */
#define MESH_LF_GPU_EXT 8u /* S_GPU_UTIL_EXT */
#define MESH_LF_THERMAL 0x10u
#define MESH_LF_TEMP 0x20u
#define MESH_LF_MEM_LEVEL 0x40u
/* Per logical CPU i < S_CPUS: u8 busy percent (1 s) at S_CORE_UTIL + i, u8 CORE_* at S_CORE_CLASS + i. */
#define MESH_MAX_CPUS 128
#define MESH_S_CORE_UTIL 0x1100
#define MESH_S_CORE_CLASS 0x1180
#define MESH_CORE_UNKNOWN 0
#define MESH_CORE_BIG 1
#define MESH_CORE_LITTLE 2

#endif
