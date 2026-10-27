/* Reading a whole file. */
#ifndef MESH_PTX_READ_H
#define MESH_PTX_READ_H
#include <stddef.h>
/* The file's bytes (NUL-terminated) and length, or NULL. Caller frees. */
char *mesh_ptx_read_file(const char *path, size_t *len);
#endif
