/* The AVL tree behind @ajax/mesh-omap: recursive insert and delete, navigation by key. */
#include "mesh/omap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct node {
    struct node *l, *r;
    int h;
    uint64_t data[]; /* the key's words, then the value */
} node;

struct momap {
    node *root;
    size_t len;
    unsigned kw;
    size_t vsize;
    momap_free_fn free_val;
    momap_copy_fn copy_val;
};

static void *xalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        fputs("mesh-omap: out of memory\n", stderr);
        abort();
    }
    return p;
}

static void *val_of(const momap *m, const node *n) { return (void *)(n->data + m->kw); }

static int cmp(const momap *m, const uint64_t *a, const uint64_t *b) {
    for (unsigned i = 0; i < m->kw; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

static int height(const node *n) { return n ? n->h : 0; }
static void fix(node *n) {
    int a = height(n->l), b = height(n->r);
    n->h = (a > b ? a : b) + 1;
}
static node *rot_right(node *n) {
    node *l = n->l;
    n->l = l->r;
    l->r = n;
    fix(n);
    fix(l);
    return l;
}
static node *rot_left(node *n) {
    node *r = n->r;
    n->r = r->l;
    r->l = n;
    fix(n);
    fix(r);
    return r;
}
static node *balance(node *n) {
    fix(n);
    int b = height(n->l) - height(n->r);
    if (b > 1) {
        if (height(n->l->l) < height(n->l->r)) n->l = rot_left(n->l);
        return rot_right(n);
    }
    if (b < -1) {
        if (height(n->r->r) < height(n->r->l)) n->r = rot_right(n->r);
        return rot_left(n);
    }
    return n;
}

momap *momap_new(unsigned key_words, size_t val_size, momap_free_fn free_val, momap_copy_fn copy_val) {
    if (key_words < 1 || key_words > MOMAP_MAX_KEY) {
        fprintf(stderr, "mesh-omap: %u key words (1..%d)\n", key_words, MOMAP_MAX_KEY);
        abort();
    }
    momap *m = xalloc(sizeof *m);
    m->kw = key_words;
    m->vsize = val_size;
    m->free_val = free_val;
    m->copy_val = copy_val;
    return m;
}

static void drop_tree(momap *m, node *n) {
    while (n) {
        drop_tree(m, n->l);
        node *r = n->r;
        if (m->free_val) m->free_val(val_of(m, n));
        free(n);
        n = r;
    }
}

void momap_clear(momap *m) {
    if (!m) return;
    drop_tree(m, m->root);
    m->root = NULL;
    m->len = 0;
}

void momap_free(momap *m) {
    if (!m) return;
    momap_clear(m);
    free(m);
}

static node *new_node(const momap *m, const uint64_t *key) {
    node *n = xalloc(sizeof(node) + 8 * m->kw + (m->vsize + 7) / 8 * 8);
    memcpy(n->data, key, 8 * m->kw);
    n->h = 1;
    return n;
}

static node *clone_tree(const momap *m, const node *n) {
    if (!n) return NULL;
    node *c = new_node(m, n->data);
    c->h = n->h;
    if (m->copy_val) m->copy_val(val_of(m, c), val_of(m, n));
    else memcpy(val_of(m, c), val_of(m, n), m->vsize);
    c->l = clone_tree(m, n->l);
    c->r = clone_tree(m, n->r);
    return c;
}

momap *momap_clone(const momap *m) {
    momap *c = momap_new(m->kw, m->vsize, m->free_val, m->copy_val);
    c->root = clone_tree(m, m->root);
    c->len = m->len;
    return c;
}

size_t momap_len(const momap *m) { return m ? m->len : 0; }
unsigned momap_key_words(const momap *m) { return m->kw; }
size_t momap_val_size(const momap *m) { return m->vsize; }

void *momap_get(const momap *m, const uint64_t *key) {
    const node *n = m->root;
    while (n) {
        int c = cmp(m, key, n->data);
        if (c == 0) return val_of(m, n);
        n = c < 0 ? n->l : n->r;
    }
    return NULL;
}

static node *insert(momap *m, node *n, const uint64_t *key, node **out) {
    if (!n) {
        *out = new_node(m, key);
        m->len++;
        return *out;
    }
    int c = cmp(m, key, n->data);
    if (c == 0) {
        *out = n;
        return n;
    }
    if (c < 0) n->l = insert(m, n->l, key, out);
    else n->r = insert(m, n->r, key, out);
    return balance(n);
}

void *momap_put(momap *m, const uint64_t *key, bool *fresh) {
    size_t before = m->len;
    node *got = NULL;
    m->root = insert(m, m->root, key, &got);
    if (fresh) *fresh = m->len != before;
    return val_of(m, got);
}

/* Detaches the smallest node of n's subtree into *min. */
static node *pop_min(node *n, node **min) {
    if (!n->l) {
        *min = n;
        return n->r;
    }
    n->l = pop_min(n->l, min);
    return balance(n);
}

static node *remove_node(momap *m, node *n, const uint64_t *key, node **gone) {
    if (!n) return NULL;
    int c = cmp(m, key, n->data);
    if (c < 0) n->l = remove_node(m, n->l, key, gone);
    else if (c > 0) n->r = remove_node(m, n->r, key, gone);
    else {
        *gone = n;
        if (!n->l) return n->r;
        if (!n->r) return n->l;
        node *min = NULL;
        node *r = pop_min(n->r, &min);
        min->l = n->l;
        min->r = r;
        return balance(min);
    }
    return balance(n);
}

bool momap_take(momap *m, const uint64_t *key, void *out) {
    node *gone = NULL;
    m->root = remove_node(m, m->root, key, &gone);
    if (!gone) return false;
    m->len--;
    if (out) memcpy(out, val_of(m, gone), m->vsize);
    else if (m->free_val) m->free_val(val_of(m, gone));
    free(gone);
    return true;
}

bool momap_del(momap *m, const uint64_t *key) {
    node *gone = NULL;
    m->root = remove_node(m, m->root, key, &gone);
    if (!gone) return false;
    m->len--;
    if (m->free_val) m->free_val(val_of(m, gone));
    free(gone);
    return true;
}

static void *give(const momap *m, const node *n, uint64_t *key_out) {
    if (!n) return NULL;
    if (key_out) memmove(key_out, n->data, 8 * m->kw);
    return val_of(m, n);
}

void *momap_first(const momap *m, uint64_t *key_out) {
    const node *n = m->root;
    while (n && n->l) n = n->l;
    return give(m, n, key_out);
}

void *momap_last(const momap *m, uint64_t *key_out) {
    const node *n = m->root;
    while (n && n->r) n = n->r;
    return give(m, n, key_out);
}

/* The nearest node to key: strictly after (dir > 0, or at it too when eq), or before (dir < 0). */
static const node *near(const momap *m, const uint64_t *key, int dir, bool eq) {
    const node *n = m->root, *best = NULL;
    while (n) {
        int c = cmp(m, n->data, key);
        if (c == 0 && eq) return n;
        if (dir > 0) {
            if (c > 0) {
                best = n;
                n = n->l;
            } else n = n->r;
        } else {
            if (c < 0) {
                best = n;
                n = n->r;
            } else n = n->l;
        }
    }
    return best;
}

void *momap_next(const momap *m, const uint64_t *after, uint64_t *key_out) { return give(m, near(m, after, 1, false), key_out); }
void *momap_prev(const momap *m, const uint64_t *before, uint64_t *key_out) { return give(m, near(m, before, -1, false), key_out); }
void *momap_floor(const momap *m, const uint64_t *at, uint64_t *key_out) { return give(m, near(m, at, -1, true), key_out); }
void *momap_ceil(const momap *m, const uint64_t *at, uint64_t *key_out) { return give(m, near(m, at, 1, true), key_out); }

momap *momap_split_off(momap *m, const uint64_t *at) {
    momap *o = momap_new(m->kw, m->vsize, m->free_val, m->copy_val);
    uint64_t k[MOMAP_MAX_KEY];
    for (;;) {
        void *v = momap_ceil(m, at, k);
        if (!v) break;
        void *dst = momap_put(o, k, NULL);
        momap_take(m, k, dst);
    }
    return o;
}
