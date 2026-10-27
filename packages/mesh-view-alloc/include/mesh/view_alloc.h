/* @ajax/mesh-view-alloc: first-fit allocation over the view heap's
 * VIEW_PAGES pages (programs.rs Heap). A view of 0 bytes takes no page
 * (offset 0). Offsets are mailbox offsets. Freeing a free page is a bug in
 * meshd: it aborts, as the Rust panics. */
#ifndef MESH_VIEW_ALLOC_H
#define MESH_VIEW_ALLOC_H

#include "mesh/abi/view_heap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool used[MESH_VIEW_PAGES];
} mesh_view_heap;

void mesh_view_heap_init(mesh_view_heap *h);
/* Pages a view of `bytes` takes. */
size_t mesh_view_pages(uint32_t bytes);
/* A view's mailbox offset (0 for 0 bytes); false when no run of pages fits. */
bool mesh_view_heap_alloc(mesh_view_heap *h, uint32_t bytes, uint32_t *off);
void mesh_view_heap_free(mesh_view_heap *h, uint32_t off, uint32_t bytes);
uint32_t mesh_view_heap_free_pages(const mesh_view_heap *h);
/* The largest run of free pages (the largest view that still loads). */
uint32_t mesh_view_heap_largest_run(const mesh_view_heap *h);

#endif
