/* A sorted string map (see the header). */
#include "mesh/strmap.h"
#include "mesh/mt.h"

#include <stdlib.h>
#include <string.h>

/* The index of key, or where it would go (*found false). */
static size_t find(const ms_map *m, const char *key, bool *found) {
    size_t lo = 0, hi = m->n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(m->k[mid], key);
        if (c == 0) {
            *found = true;
            return mid;
        }
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    *found = false;
    return lo;
}

const char *ms_get(const ms_map *m, const char *key) {
    bool f;
    size_t i = find(m, key, &f);
    return f ? m->v[i] : NULL;
}

bool ms_set(ms_map *m, const char *key, const char *value, bool replace) {
    bool f;
    size_t i = find(m, key, &f);
    if (f) {
        if (!replace) return false;
        mt_set(&m->v[i], value);
        return true;
    }
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 8;
        m->k = mt_realloc(m->k, m->cap * sizeof *m->k);
        m->v = mt_realloc(m->v, m->cap * sizeof *m->v);
    }
    memmove(m->k + i + 1, m->k + i, (m->n - i) * sizeof *m->k);
    memmove(m->v + i + 1, m->v + i, (m->n - i) * sizeof *m->v);
    m->k[i] = mt_strdup(key);
    m->v[i] = mt_strdup(value);
    m->n++;
    return true;
}

bool ms_del(ms_map *m, const char *key) {
    bool f;
    size_t i = find(m, key, &f);
    if (!f) return false;
    free(m->k[i]);
    free(m->v[i]);
    memmove(m->k + i, m->k + i + 1, (m->n - i - 1) * sizeof *m->k);
    memmove(m->v + i, m->v + i + 1, (m->n - i - 1) * sizeof *m->v);
    m->n--;
    return true;
}

void ms_free(ms_map *m) {
    for (size_t i = 0; i < m->n; i++) {
        free(m->k[i]);
        free(m->v[i]);
    }
    free(m->k);
    free(m->v);
    *m = (ms_map){0};
}
