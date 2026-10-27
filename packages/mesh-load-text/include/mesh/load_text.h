/* @ajax/mesh-load-text: the control port's `load` request (load.rs `text`):
 * this machine's load (GPU share, CPUs by class, PSI, memory, meshd, the CPU
 * executors, disk, thermal) and each loaded program's, as `key value` lines,
 * read from the host status block the load sampler fills. */
#ifndef MESH_LOAD_TEXT_H
#define MESH_LOAD_TEXT_H

#include "mesh/buf.h"
#include "mesh/shared.h"

void mesh_load_text(mesh_shared *sh, mesh_buf *out);

#endif
