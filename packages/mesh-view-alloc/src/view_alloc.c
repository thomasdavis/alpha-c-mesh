/* First fit over a page map. */
#include "mesh/view_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mesh_view_heap_init(mesh_view_heap *h) { memset(h->used, 0, sizeof h->used); }

size_t mesh_view_pages(uint32_t bytes) { return ((size_t)bytes + MESH_VIEW_PAGE - 1) / MESH_VIEW_PAGE; }

bool mesh_view_heap_alloc(mesh_view_heap *h, uint32_t bytes, uint32_t *off) {
    size_t n = mesh_view_pages(bytes);
    if (n == 0) { *off = 0; return true; }
    size_t run = 0;
    for (size_t p = 0; p < MESH_VIEW_PAGES; p++) {
        run = h->used[p] ? 0 : run + 1;
        if (run == n) {
            size_t first = p + 1 - n;
            for (size_t i = first; i <= p; i++) h->used[i] = true;
            *off = (uint32_t)(MESH_VIEW_HEAP + first * MESH_VIEW_PAGE);
            return true;
        }
    }
    return false;
}

void mesh_view_heap_free(mesh_view_heap *h, uint32_t off, uint32_t bytes) {
    size_t n = mesh_view_pages(bytes);
    if (n == 0) return;
    size_t first = ((size_t)off - MESH_VIEW_HEAP) / MESH_VIEW_PAGE;
    for (size_t i = first; i < first + n; i++) {
        if (!h->used[i]) {
            fprintf(stderr, "view heap: freeing a free page at %#x\n", off);
            abort();
        }
        h->used[i] = false;
    }
}

uint32_t mesh_view_heap_free_pages(const mesh_view_heap *h) {
    uint32_t n = 0;
    for (size_t p = 0; p < MESH_VIEW_PAGES; p++) n += !h->used[p];
    return n;
}

uint32_t mesh_view_heap_largest_run(const mesh_view_heap *h) {
    uint32_t best = 0, run = 0;
    for (size_t p = 0; p < MESH_VIEW_PAGES; p++) {
        run = h->used[p] ? 0 : run + 1;
        if (run > best) best = run;
    }
    return best;
}
