/* @ajax/mesh-strmap: a map from strings to strings, kept sorted by key (bytewise, as Rust's
 * BTreeMap<String, String> orders), so iterating it is in Rust's order. */
#ifndef MESH_STRMAP_H
#define MESH_STRMAP_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char **k, **v;
    size_t n, cap;
} ms_map;

const char *ms_get(const ms_map *m, const char *key); /* NULL when absent */
/* Sets key to value (copies). Returns false (changing nothing) if the key existed and !replace. */
bool ms_set(ms_map *m, const char *key, const char *value, bool replace);
bool ms_del(ms_map *m, const char *key);
void ms_free(ms_map *m);

#endif
