/* Freeing a decoded module, and finding exports. */
#include "wasm/module.h"

#include <stdlib.h>
#include <string.h>

void wasm_module_free(wasm_module *m) {
    if (!m) return;
    free(m->types);
    free(m->imports);
    free(m->funcs);
    free(m->tables);
    free(m->mems);
    free(m->globals);
    free(m->global_inits);
    free(m->exports);
    if (m->elems)
        for (uint32_t i = 0; i < m->nelems; i++) {
            free(m->elems[i].funcs);
            free(m->elems[i].exprs);
        }
    free(m->elems);
    if (m->codes)
        for (uint32_t i = 0; i < m->ncodes; i++) free(m->codes[i].locals);
    free(m->codes);
    free(m->datas);
    memset(m, 0, sizeof *m);
}

const wasm_export *wasm_module_export(const wasm_module *m, const char *name, uint8_t kind) {
    size_t n = strlen(name);
    for (uint32_t i = 0; i < m->nexports; i++) {
        const wasm_export *e = &m->exports[i];
        if (e->kind == kind && e->name_len == n && memcmp(e->name, name, n) == 0) return e;
    }
    return NULL;
}
