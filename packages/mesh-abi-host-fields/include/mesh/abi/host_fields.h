/* @ajax/mesh-abi-host-fields: Every field of the host status block, as (offset, bytes): the table the layout checks run over.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_FIELDS_H
#define MESH_ABI_HOST_FIELDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"
#include "mesh/abi/host_status.h"
#include "mesh/abi/host_members.h"
#include "mesh/abi/host_net.h"
#include "mesh/abi/host_links.h"
#include "mesh/abi/host_nics.h"
#include "mesh/abi/host_load.h"
#include "mesh/abi/host_mload.h"
#include "mesh/abi/host_fleet.h"
#include "mesh/abi/host_sched.h"
#include "mesh/abi/host_jobs.h"
#include "mesh/abi/host_pload.h"
#include "mesh/abi/fields.h"

#define MESH_HOST_FIELDS_LEN 138
extern const mesh_field MESH_HOST_FIELDS[138];

#endif
