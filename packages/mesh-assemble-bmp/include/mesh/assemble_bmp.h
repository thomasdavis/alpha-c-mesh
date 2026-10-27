/* @ajax/mesh-assemble-bmp: a 24-bit BMP of an RGB raster (assemble.rs `bmp`): rows bottom-up, BGR,
 * each padded to 4 bytes. */
#ifndef MESH_ASSEMBLE_BMP_H
#define MESH_ASSEMBLE_BMP_H

#include <stddef.h>
#include <stdint.h>

/* w x h pixels of 3 bytes (row-major, top row first); a new buffer of *len bytes. */
uint8_t *masm_bmp(uint32_t w, uint32_t h, const uint8_t *raster, size_t *len);

#endif
