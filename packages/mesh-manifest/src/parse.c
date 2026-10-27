/* manifest.rs's `parse`: every key, default and refusal, in the Rust's order
 * (so the first refusal is the same one), with the Rust's messages. */
#include "mesh/manifest.h"
#include "mesh/toml.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/name.h"
#include "mesh/abi/node.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/pipe_ring.h"
#include "mesh/abi/placement.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/view_heap.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const KEYS[] = {
    "name", "abi", "view_bytes", "view_abi", "snap_bytes", "nodes", "warps", "shared_bytes", "scratch_bytes", "sms",
    "policy", "hz", "fanout", "in_flight", "arg", "reads", "input", "request", "peer", "counters", "fields",
    "autostart", "place", "place_state", "place_frame", "place_out", "place_show", "backends", "param", "float",
    "mem_mb", "task_ms", "cpu", "nice", "background", "inbox", "inbox_type", "feeds", "replicas", "spread", "prefer",
    "avoid", "arch", "min_mem_mb", "colocate_with", "group", "group_size", "group_members", "priority", "migrate",
    "job_base", "job_out", "job_payload", "device"};
_Static_assert(sizeof KEYS / sizeof KEYS[0] == 54, "KEYS: [&str; 54]");
static const char *const NATIVE_KEYS[] = {"nice", "background", "device"};

typedef struct {
    char *key;
    mesh_toml_val val;
} kv;

/* The parse's state: the key/values, the message being built, temporaries. */
typedef struct {
    kv *kvs;
    size_t nkv;
    mesh_text msg;
    uint32_t view_bytes;
} P;

static const mesh_toml_val *get(const P *p, const char *k) {
    for (size_t i = 0; i < p->nkv; i++)
        if (strcmp(p->kvs[i].key, k) == 0) return &p->kvs[i].val;
    return NULL;
}

/* Message building: printf, {:?} of a string, {:?} of a value. */
static void m_(P *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void m_(P *p, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mesh_text_vprintf(&p->msg, fmt, ap);
    va_end(ap);
}
static void q_(P *p, const char *s) { mesh_text_debug_str(&p->msg, s, strlen(s)); }
static void qn_(P *p, const char *s, size_t n) { mesh_text_debug_str(&p->msg, s, n); }
static void v_(P *p, const mesh_toml_val *v) { mesh_toml_val_debug(&p->msg, v); }
static void list_(P *p, const char *const *v, size_t n) {
    mesh_text_putc(&p->msg, '[');
    for (size_t i = 0; i < n; i++) {
        if (i) mesh_text_puts(&p->msg, ", ");
        q_(p, v[i]);
    }
    mesh_text_putc(&p->msg, ']');
}
static bool fail_(P *p, char **err) {
    if (err) {
        free(*err);
        *err = mesh_text_take(&p->msg);
    } else {
        mesh_text_free(&p->msg);
    }
    return false;
}

#define FAIL(...)              \
    do {                       \
        m_(p, __VA_ARGS__);    \
        return fail_(p, err);  \
    } while (0)

static bool num(P *p, const char *k, uint32_t d, uint32_t *out, char **err) {
    const mesh_toml_val *v = get(p, k);
    if (!v) { *out = d; return true; }
    if (v->kind == MESH_TOML_INT) { *out = v->n; return true; }
    m_(p, "%s: expected a number, got ", k);
    v_(p, v);
    return fail_(p, err);
}

/* A string value or the default; *out points into the parse (not owned). */
static bool text(P *p, const char *k, const char *d, const char **out, char **err) {
    const mesh_toml_val *v = get(p, k);
    if (!v) { *out = d; return true; }
    if (v->kind == MESH_TOML_STR) { *out = v->s; return true; }
    m_(p, "%s: expected a string, got ", k);
    v_(p, v);
    return fail_(p, err);
}

static char *const no_items[1] = {NULL};

static bool list(P *p, const char *k, char *const **items, size_t *n, char **err) {
    const mesh_toml_val *v = get(p, k);
    if (!v) { *items = no_items; *n = 0; return true; }
    if (v->kind == MESH_TOML_LIST) { *items = v->items; *n = v->nitems; return true; }
    m_(p, "%s: expected a list, got ", k);
    v_(p, v);
    return fail_(p, err);
}

static bool in_view(P *p, const char *what, uint32_t off, uint32_t len, char **err) {
    if ((uint64_t)off + len > p->view_bytes)
        FAIL("%s: 0x%x+0x%x is outside view_bytes 0x%x", what, (unsigned)off, (unsigned)len, (unsigned)p->view_bytes);
    return true;
}

static bool valid_type(const char *t) {
    size_t n = strlen(t);
    if (n == 0 || n > MESH_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (!((t[i] >= 'a' && t[i] <= 'z') || (t[i] >= '0' && t[i] <= '9') || t[i] == '_')) return false;
    return true;
}

static bool vname(const char *s) { return mesh_valid_name((const uint8_t *)s, strlen(s)); }
static bool pair(const char *s, uint32_t *a, uint32_t *b) { return mesh_toml_pair(s, strlen(s), a, b); }
static bool ints(const char *s, size_t n, uint32_t *out) { return mesh_toml_int(s, n, out); }
static bool is_pow2(uint32_t x) { return x != 0 && (x & (x - 1)) == 0; }

static int position(const char *const *v, size_t n, const char *s) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(v[i], s) == 0) return (int)i;
    return -1;
}

static bool term_list(P *p, const char *k, mesh_term **out, size_t *nout, char **err) {
    char *const *items;
    size_t n;
    if (!list(p, k, &items, &n, err)) return false;
    *out = n ? mesh_alloc(n * sizeof **out) : NULL;
    *nout = 0;
    for (size_t i = 0; i < n; i++) {
        char *e = NULL;
        if (!mesh_term_parse(items[i], &(*out)[i], &e)) {
            m_(p, "%s: %s", k, e);
            free(e);
            for (size_t j = 0; j < i; j++) mesh_term_free(&(*out)[j]);
            free(*out);
            *out = NULL;
            return fail_(p, err);
        }
        (*nout)++;
    }
    return true;
}

typedef struct {
    uint32_t off, len;
    const char *what;
} span;

static bool overlaps(span a, span b) { return a.off < b.off + b.len && b.off < a.off + a.len; }

static bool parse_kv(P *p, const char *text, size_t n, char **err) {
    size_t lineno = 0;
    size_t i = 0;
    while (i < n) {
        /* Rust's str::lines: split at \n, drop one \r before it, no empty last line. */
        const char *line = text + i;
        const char *nl = memchr(line, '\n', n - i);
        size_t len = nl ? (size_t)(nl - line) : n - i;
        i += len + (nl ? 1 : 0);
        if (len > 0 && line[len - 1] == '\r') len--;
        lineno++;
        size_t ln = mesh_toml_strip_comment(line, len);
        const char *l = line;
        mesh_trim(&l, &ln);
        if (ln == 0) continue;
        const char *eq = memchr(l, '=', ln);
        if (!eq) {
            m_(p, "line %zu: expected key = value: ", lineno);
            qn_(p, l, ln);
            return fail_(p, err);
        }
        const char *k = l;
        size_t kn = (size_t)(eq - l);
        mesh_trim(&k, &kn);
        char *key = mesh_strndup(k, kn);
        if (position(KEYS, sizeof KEYS / sizeof KEYS[0], key) < 0) {
            m_(p, "line %zu: unknown key ", lineno);
            q_(p, key);
            free(key);
            return fail_(p, err);
        }
        if (get(p, key)) {
            m_(p, "line %zu: repeated key ", lineno);
            q_(p, key);
            free(key);
            return fail_(p, err);
        }
        mesh_toml_val v;
        char *e = NULL;
        if (!mesh_toml_value(eq + 1, (size_t)(l + ln - (eq + 1)), &v, &e)) {
            m_(p, "line %zu: %s", lineno, e);
            free(e);
            free(key);
            return fail_(p, err);
        }
        p->kvs = mesh_realloc(p->kvs, (p->nkv + 1) * sizeof *p->kvs);
        p->kvs[p->nkv].key = key;
        p->kvs[p->nkv].val = v;
        p->nkv++;
    }
    return true;
}

static bool parse(P *p, const char *src, size_t srcn, mesh_manifest *m, char **err) {
    if (!parse_kv(p, src, srcn, err)) return false;
    const mesh_toml_val *v;

    /* name, abi */
    v = get(p, "name");
    if (!v) FAIL("name: missing");
    if (v->kind != MESH_TOML_STR) {
        m_(p, "name: expected a string, got ");
        v_(p, v);
        return fail_(p, err);
    }
    m->name = mesh_strdup(v->s);
    if (!vname(m->name)) {
        m_(p, "name ");
        q_(p, m->name);
        FAIL(": must be [a-z0-9-]{1,%d}", MESH_NAME_MAX);
    }
    v = get(p, "abi");
    if (!v) FAIL("abi: missing");
    if (v->kind != MESH_TOML_INT) {
        m_(p, "abi: expected a number, got ");
        v_(p, v);
        return fail_(p, err);
    }
    m->abi = v->n;

    /* the view */
    if (!num(p, "view_bytes", 0, &m->view_bytes, err)) return false;
    if ((size_t)m->view_bytes > (size_t)MESH_VIEW_PAGES * MESH_VIEW_PAGE)
        FAIL("view_bytes 0x%x: the view heap has 0x%zx", (unsigned)m->view_bytes, (size_t)MESH_VIEW_PAGES * MESH_VIEW_PAGE);
    p->view_bytes = m->view_bytes;
    if (!num(p, "snap_bytes", 0, &m->snap_bytes, err)) return false;
    if (!in_view(p, "snap_bytes", 0, m->snap_bytes, err)) return false;

    /* the node shape */
    v = get(p, "nodes");
    m->nodes_fit = v && v->kind == MESH_TOML_STR && strcmp(v->s, "fit") == 0;
    if (m->nodes_fit) m->nodes = MESH_PROG_MAX_NODES;
    else if (!num(p, "nodes", 1, &m->nodes, err)) return false;
    if (m->nodes == 0 || m->nodes > MESH_PROG_MAX_NODES) FAIL("nodes %u: must be 1..=%d", (unsigned)m->nodes, MESH_PROG_MAX_NODES);
    if (!num(p, "warps", 1, &m->warps, err)) return false;
    if (m->warps == 0 || m->warps > MESH_MAX_WARPS)
        FAIL("warps %u: must be 1..=%u (a node of 32 x warps threads)", (unsigned)m->warps, (unsigned)MESH_MAX_WARPS);
    if (!num(p, "shared_bytes", 0, &m->shared_bytes, err)) return false;
    if (m->shared_bytes > MESH_MAX_SHARED_BYTES || m->shared_bytes % MESH_SHARED_ALIGN != 0)
        FAIL("shared_bytes 0x%x: a multiple of %u, at most 0x%x", (unsigned)m->shared_bytes, (unsigned)MESH_SHARED_ALIGN,
             (unsigned)MESH_MAX_SHARED_BYTES);
    if (!num(p, "scratch_bytes", 0, &m->scratch_bytes, err)) return false;
    if (m->scratch_bytes > MESH_MAX_SCRATCH_BYTES)
        FAIL("scratch_bytes 0x%x: at most 0x%x", (unsigned)m->scratch_bytes, (unsigned)MESH_MAX_SCRATCH_BYTES);
    if (!num(p, "sms", 0, &m->sms, err)) return false;
    if (m->sms % 2 != 0) FAIL("sms %u: green partitions come in 2-SM units", (unsigned)m->sms);

    /* the policy */
    const char *s;
    if (!text(p, "policy", "on_demand", &s, err)) return false;
    int pos = position(MESH_POLICIES, MESH_POLICIES_LEN, s);
    if (pos < 0) {
        m_(p, "policy ");
        q_(p, s);
        m_(p, ": one of ");
        list_(p, MESH_POLICIES, MESH_POLICIES_LEN);
        return fail_(p, err);
    }
    m->policy = (uint32_t)pos;
    if (!num(p, "hz", 0, &m->hz, err)) return false;
    if (m->policy == MESH_POLICY_PERIODIC && !(m->hz >= 1 && m->hz <= 1000)) FAIL("hz %u: periodic needs 1..=1000", (unsigned)m->hz);
    if (m->policy != MESH_POLICY_PERIODIC && get(p, "hz")) FAIL("hz: only for policy = \"periodic\"");
    if (!num(p, "fanout", 1, &m->fanout, err)) return false;
    if (m->fanout == 0 || m->fanout > MESH_PROG_RING / 2) FAIL("fanout %u: must be 1..=%u", (unsigned)m->fanout, (unsigned)(MESH_PROG_RING / 2));
    if (!text(p, "in_flight", "batch", &s, err)) return false;
    if (strcmp(s, "batch") == 0) m->batch = true;
    else if (strcmp(s, "free") == 0) m->batch = false;
    else {
        m_(p, "in_flight ");
        q_(p, s);
        FAIL(": \"batch\" or \"free\"");
    }
    const char *arg_s;
    if (!text(p, "arg", "index", &arg_s, err)) return false;
    const char *colon = strchr(arg_s, ':');
    if (!colon && strcmp(arg_s, "index") == 0) {
        m->arg_mode = MESH_ARG_INDEX;
        m->arg_off = 0;
    } else if (colon && (size_t)(colon - arg_s) == 8 && memcmp(arg_s, "view_u32", 8) == 0) {
        uint32_t o;
        if (!ints(colon + 1, strlen(colon + 1), &o)) {
            m_(p, "arg ");
            q_(p, arg_s);
            FAIL(": bad offset");
        }
        if (o % 4 != 0) {
            m_(p, "arg ");
            q_(p, arg_s);
            FAIL(": offset not 4-aligned");
        }
        if (!in_view(p, "arg", o, 4, err)) return false;
        m->arg_mode = MESH_ARG_VIEW_U32;
        m->arg_off = o;
    } else {
        m_(p, "arg ");
        q_(p, arg_s);
        FAIL(": \"index\" or \"view_u32:<off>\"");
    }

    /* reads */
    char *const *items;
    size_t nitems;
    if (!list(p, "reads", &items, &nitems, err)) return false;
    for (size_t i = 0; i < nitems; i++) {
        const char *r = items[i];
        if (strcmp(r, "*") == 0) {
            if (nitems != 1 || strcmp(m->name, "http") != 0) FAIL("reads [\"*\"]: only for http, and alone");
        } else if (!vname(r) || strcmp(r, m->name) == 0) {
            m_(p, "reads ");
            q_(p, r);
            FAIL(": another program's name");
        }
    }
    m->reads = mesh_strs_copy(items, nitems);
    m->nreads = nitems;

    /* input */
    v = get(p, "input");
    if (!v || (v->kind == MESH_TOML_INT && v->n == 0)) {
        m->input_off = m->input_len = 0;
    } else if (v->kind == MESH_TOML_INT) {
        uint32_t need = (uint32_t)MESH_IQ_DATA + v->n; /* wraps as the Rust release build */
        if (m->view_bytes < need) FAIL("input: does not fit the view");
        m->input_off = (m->view_bytes - need) & ~15u;
        m->input_len = v->n;
    } else if (v->kind == MESH_TOML_STR) {
        if (!pair(v->s, &m->input_off, &m->input_len)) {
            m_(p, "input ");
            q_(p, v->s);
            FAIL(": \"off:len\"");
        }
    } else {
        m_(p, "input: expected a number or \"off:len\", got ");
        v_(p, v);
        return fail_(p, err);
    }
    if (m->input_len != 0) {
        if (!is_pow2(m->input_len)) FAIL("input: %u bytes is not a power of two", (unsigned)m->input_len);
        if (m->input_off % 16 != 0) FAIL("input: offset 0x%x not 16-aligned", (unsigned)m->input_off);
        if (!in_view(p, "input", m->input_off, (uint32_t)MESH_IQ_DATA + m->input_len, err)) return false;
    }

    /* request */
    v = get(p, "request");
    if (v && v->kind == MESH_TOML_STR) {
        const char *comma = strchr(v->s, ',');
        mesh_manifest_window w;
        bool ok = comma != NULL;
        if (ok) {
            char *a = mesh_strndup(v->s, (size_t)(comma - v->s));
            ok = pair(a, &w.req_off, &w.req_max) && pair(comma + 1, &w.resp_off, &w.resp_max);
            free(a);
        }
        if (!ok) {
            m_(p, "request ");
            q_(p, v->s);
            FAIL(": \"req_off:req_max,resp_off:resp_max\"");
        }
        if (!in_view(p, "request", w.req_off, w.req_max, err)) return false;
        if (!in_view(p, "request response", w.resp_off, w.resp_max, err)) return false;
        if (w.req_off < w.resp_off + w.resp_max && w.resp_off < w.req_off + w.req_max) {
            m_(p, "request ");
            q_(p, v->s);
            FAIL(": windows overlap");
        }
        if (w.req_max == 0 || w.resp_max == 0 || w.req_off % 4 != 0 || w.resp_off % 4 != 0) {
            m_(p, "request ");
            q_(p, v->s);
            FAIL(": empty or unaligned window");
        }
        m->has_request = true;
        m->request = w;
    } else if (v) {
        m_(p, "request: expected a string, got ");
        v_(p, v);
        return fail_(p, err);
    }

    /* peer */
    if (!text(p, "peer", "none", &s, err)) return false;
    pos = position(MESH_PEER_MODES, MESH_PEER_MODES_LEN, s);
    if (pos < 0) {
        m_(p, "peer ");
        q_(p, s);
        m_(p, ": one of ");
        list_(p, MESH_PEER_MODES, MESH_PEER_MODES_LEN);
        return fail_(p, err);
    }
    m->peer = (uint32_t)pos;
    bool lockstep = m->peer == MESH_PEER_MODE_LOCKSTEP;
    if (lockstep && (m->nodes != 1 || m->fanout != 1))
        FAIL("peer \"lockstep\": needs nodes = 1 and fanout = 1 (the exchange page has one writer per system), got %u and %u",
             (unsigned)m->nodes, (unsigned)m->fanout);

    /* counters, fields, autostart */
    if (!list(p, "counters", &items, &nitems, err)) return false;
    if (nitems > MESH_COUNTERS) FAIL("counters: at most %d", MESH_COUNTERS);
    for (size_t i = 0; i < nitems; i++)
        if (strlen(items[i]) > MESH_NAME_MAX) {
            m_(p, "counters ");
            q_(p, items[i]);
            FAIL(": at most %d bytes", MESH_NAME_MAX);
        }
    for (size_t i = 0; i < MESH_COUNTERS; i++) m->counters[i] = mesh_strdup(i < nitems ? items[i] : "");
    if (!list(p, "fields", &items, &nitems, err)) return false;
    for (size_t i = 0; i < nitems; i++) {
        const char *f = items[i];
        /* name:type:off, exactly three parts */
        const char *c1 = strchr(f, ':');
        const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        bool three = c1 && c2 && !strchr(c2 + 1, ':');
        if (!three) {
            m_(p, "fields ");
            q_(p, f);
            m_(p, ": \"name:type:off\", type one of ");
            list_(p, MESH_FIELD_TYPES, MESH_FIELD_TYPES_LEN);
            return fail_(p, err);
        }
        size_t nn = (size_t)(c1 - f);
        bool name_ok = nn > 0 && nn <= MESH_FD_NAME_MAX;
        for (size_t k = 0; name_ok && k < nn; k++)
            name_ok = (f[k] >= 'a' && f[k] <= 'z') || (f[k] >= '0' && f[k] <= '9') || f[k] == '_';
        if (!name_ok) {
            m_(p, "fields ");
            q_(p, f);
            FAIL(": name must be [a-z0-9_]{1,%d}", MESH_FD_NAME_MAX);
        }
        char *ty = mesh_strndup(c1 + 1, (size_t)(c2 - c1 - 1));
        int t = position(MESH_FIELD_TYPES, MESH_FIELD_TYPES_LEN, ty);
        free(ty);
        uint32_t off;
        if (t < 0 || !ints(c2 + 1, strlen(c2 + 1), &off)) {
            m_(p, "fields ");
            q_(p, f);
            m_(p, ": \"name:type:off\", type one of ");
            list_(p, MESH_FIELD_TYPES, MESH_FIELD_TYPES_LEN);
            return fail_(p, err);
        }
        static const uint32_t sizes[] = {4, 4, 4, 8, 1};
        uint32_t size = sizes[t];
        if (off % size != 0) {
            m_(p, "fields ");
            q_(p, f);
            FAIL(": offset not %u-aligned", (unsigned)size);
        }
        if (!in_view(p, "fields", off, size, err)) return false;
        m->fields = mesh_realloc(m->fields, (m->nfields + 1) * sizeof *m->fields);
        m->fields[m->nfields++] = (mesh_manifest_field){mesh_strndup(f, nn), (uint32_t)t, off};
    }
    if (m->nfields > MESH_MAX_FIELDS) FAIL("fields: at most %d", MESH_MAX_FIELDS);
    v = get(p, "autostart");
    if (v && v->kind != MESH_TOML_BOOL) {
        m_(p, "autostart: expected true/false, got ");
        v_(p, v);
        return fail_(p, err);
    }
    m->autostart = v && v->b;

    /* place */
    if (!text(p, "place", "none", &s, err)) return false;
    pos = position(MESH_PLACE_CAPS, MESH_PLACE_CAPS_LEN, s);
    if (pos < 0) {
        m_(p, "place ");
        q_(p, s);
        m_(p, ": one of ");
        list_(p, MESH_PLACE_CAPS, MESH_PLACE_CAPS_LEN);
        return fail_(p, err);
    }
    uint32_t cap = (uint32_t)pos;
    if (cap == MESH_PLACE_CAP_NONE) {
        static const char *const pk[] = {"place_state", "place_frame", "place_out", "place_show"};
        for (size_t i = 0; i < 4; i++)
            if (get(p, pk[i])) FAIL("%s: only with place = \"pin\" or \"split\"", pk[i]);
    } else {
        mesh_manifest_place *pl = mesh_alloc(sizeof *pl);
        m->place = pl;
        pl->cap = cap;
        static const char *const rk[] = {"place_state", "place_out"};
        for (int which = 0; which < 2; which++) {
            const char *k = rk[which];
            if (!list(p, k, &items, &nitems, err)) return false;
            if (nitems == 0) FAIL("%s: missing (a list of \"off:len\")", k);
            mesh_manifest_region *r = mesh_alloc(nitems * sizeof *r);
            if (which == 0) { pl->state = r; pl->nstate = 0; } else { pl->out = r; pl->nout = 0; }
            for (size_t i = 0; i < nitems; i++) {
                uint32_t o, n;
                if (!pair(items[i], &o, &n)) {
                    m_(p, "%s ", k);
                    q_(p, items[i]);
                    FAIL(": \"off:len\"");
                }
                if (o % 4 != 0 || n % 4 != 0 || n == 0) {
                    m_(p, "%s ", k);
                    q_(p, items[i]);
                    FAIL(": offset and length 4-aligned, length > 0");
                }
                if (!in_view(p, k, o, n, err)) return false;
                r[i] = (mesh_manifest_region){o, n};
                if (which == 0) pl->nstate++; else pl->nout++;
            }
        }
        v = get(p, "place_frame");
        if (!v) FAIL("place_frame: missing");
        if (v->kind != MESH_TOML_INT) {
            m_(p, "place_frame: expected a number, got ");
            v_(p, v);
            return fail_(p, err);
        }
        pl->frame = v->n;
        v = get(p, "place_show");
        if (!v) FAIL("place_show: missing");
        if (v->kind != MESH_TOML_INT) {
            m_(p, "place_show: expected a number, got ");
            v_(p, v);
            return fail_(p, err);
        }
        pl->show = v->n;
        pl->fanout = m->fanout;
        if (m->policy == MESH_POLICY_ON_DEMAND || !m->batch)
            FAIL("place: needs policy \"periodic\" or \"continuous\" with in_flight = \"batch\" (a frame is one batch)");
        if (m->peer != MESH_PEER_MODE_NONE) FAIL("place: not with peer = \"lockstep\"");
        if (cap == MESH_PLACE_CAP_SPLIT && !(m->fanout >= 2 && m->fanout <= MESH_PLACE_PARTS_MAX))
            FAIL("place = \"split\": fanout %u: 2..=%d parts", (unsigned)m->fanout, (int)MESH_PLACE_PARTS_MAX);
        bool in_state = false;
        for (size_t i = 0; i < pl->nstate; i++) {
            uint32_t o = pl->state[i].off, n = pl->state[i].len;
            if (pl->frame % 4 == 0 && pl->frame >= o && (uint32_t)(pl->frame + 4) <= (uint32_t)(o + n)) in_state = true;
        }
        if (!in_state) FAIL("place_frame 0x%x: a 4-aligned u32 inside a place_state region", (unsigned)pl->frame);
        for (size_t i = 0; i < pl->nout; i++)
            if (pl->out[i].len % (4 * m->fanout) != 0)
                FAIL("place_out 0x%x:0x%x: length not a multiple of 4 x fanout", (unsigned)pl->out[i].off, (unsigned)pl->out[i].len);
        if (pl->show % 16 != 0) FAIL("place_show 0x%x: not 16-aligned", (unsigned)pl->show);
        uint32_t show_n = mesh_show_bytes(mesh_manifest_place_out_bytes(pl));
        if (!in_view(p, "place_show", pl->show, show_n, err)) return false;
        if (mesh_manifest_place_state_bytes(pl) + mesh_manifest_place_out_bytes(pl) > (size_t)MESH_F_PLACE_MAX - 64)
            FAIL("place: state + output over F_PLACE_MAX (%d bytes)", (int)MESH_F_PLACE_MAX);
        size_t nall = pl->nstate + pl->nout + 1 + (m->input_len != 0);
        span *all = mesh_alloc(nall * sizeof *all);
        size_t k = 0;
        for (size_t i = 0; i < pl->nstate; i++) all[k++] = (span){pl->state[i].off, pl->state[i].len, "place_state"};
        for (size_t i = 0; i < pl->nout; i++) all[k++] = (span){pl->out[i].off, pl->out[i].len, "place_out"};
        all[k++] = (span){pl->show, show_n, "place_show"};
        if (m->input_len != 0) all[k++] = (span){m->input_off, (uint32_t)MESH_IQ_DATA + m->input_len, "input"};
        for (size_t i = 0; i < nall; i++)
            for (size_t j = i + 1; j < nall; j++)
                if (overlaps(all[i], all[j])) {
                    m_(p, "place: %s 0x%x+0x%x overlaps %s 0x%x+0x%x", all[i].what, (unsigned)all[i].off, (unsigned)all[i].len,
                       all[j].what, (unsigned)all[j].off, (unsigned)all[j].len);
                    free(all);
                    return fail_(p, err);
                }
        free(all);
    }

    /* backends */
    if (!get(p, "backends")) {
        m->backends = mesh_alloc(sizeof *m->backends);
        m->backends[0] = mesh_strdup(MESH_BACKENDS[MESH_BACKEND_CUDA]);
        m->nbackends = 1;
    } else {
        if (!list(p, "backends", &items, &nitems, err)) return false;
        m->backends = mesh_strs_copy(items, nitems);
        m->nbackends = nitems;
    }
    if (m->nbackends == 0) {
        m_(p, "backends: at least one of ");
        list_(p, MESH_BACKENDS, MESH_BACKENDS_LEN);
        return fail_(p, err);
    }
    for (size_t i = 0; i < m->nbackends; i++) {
        const char *b = m->backends[i];
        if (position(MESH_BACKENDS, MESH_BACKENDS_LEN, b) < 0) {
            m_(p, "backends ");
            q_(p, b);
            m_(p, ": one of ");
            list_(p, MESH_BACKENDS, MESH_BACKENDS_LEN);
            return fail_(p, err);
        }
        if (position((const char *const *)m->backends, i, b) >= 0) {
            m_(p, "backends: ");
            q_(p, b);
            FAIL(" twice");
        }
    }
    bool native = mesh_manifest_native(m);
    if (native && m->nbackends > 1) FAIL("backends: \"native\" stands alone (a native program is an executable, not a GPU image)");
    if (native && lockstep) FAIL("peer \"lockstep\": not for a native program (platform float, not the cluster float mode)");
    if (!native)
        for (size_t i = 0; i < 3; i++)
            if (get(p, NATIVE_KEYS[i])) FAIL("%s: only for backends = [\"native\"]", NATIVE_KEYS[i]);
    if (!num(p, "param", 0, &m->param, err)) return false;
    bool wasm = position((const char *const *)m->backends, m->nbackends, "wasm") >= 0;
    if (native && get(p, "float")) FAIL("float: not for a native program (it computes in platform float)");
    if (!text(p, "float", "ftz", &s, err)) return false;
    if (strcmp(s, "ftz") == 0) m->ftz = true;
    else if (strcmp(s, "ieee") == 0) m->ftz = false;
    else {
        m_(p, "float ");
        q_(p, s);
        FAIL(": \"ftz\" or \"ieee\"");
    }
    if (!m->ftz && lockstep)
        FAIL("float = \"ieee\": not with peer = \"lockstep\" (the lockstep group computes in the cluster float mode, ftz)");
    if (!m->ftz)
        for (size_t i = 0; i < m->nbackends; i++)
            if (strcmp(m->backends[i], "wasm") != 0)
                FAIL("float = \"ieee\": only for a wasm-only program (backends = [\"wasm\"]): the GPUs compute in ftz");
    if (!wasm && get(p, "task_ms")) FAIL("task_ms: only for a program with a wasm image (backends lists \"wasm\")");
    if (!wasm && !native && get(p, "mem_mb")) FAIL("mem_mb: only for a wasm image or a native program");
    if (native) {
        v = get(p, "mem_mb");
        if (!v) {
            m->mem_mb = 0;
            m->mem = 0;
        } else if (v->kind == MESH_TOML_INT && v->n >= 1) {
            m->mem_mb = v->n;
            m->mem = (uint64_t)v->n * (1u << 20);
        } else {
            m_(p, "mem_mb: MiB, at least 1 (e.g. 4096), got ");
            v_(p, v);
            return fail_(p, err);
        }
    } else {
        if (!num(p, "mem_mb", 64, &m->mem_mb, err)) return false;
        if (!(m->mem_mb >= 1 && m->mem_mb <= 4096)) FAIL("mem_mb %u: 1..=4096", (unsigned)m->mem_mb);
        m->mem = 0;
    }
    if (!num(p, "task_ms", 50, &m->task_ms, err)) return false;
    if (!(m->task_ms >= 1 && m->task_ms <= 60000)) FAIL("task_ms %u: 1..=60000", (unsigned)m->task_ms);
    if (get(p, "cpu") && !native && !wasm)
        FAIL("cpu: only for a native program (a cap and a reservation) or a wasm image (a reservation)");
    uint32_t cpu_pct = 0;
    v = get(p, "cpu");
    if (v) {
        if (v->kind == MESH_TOML_INT && v->n >= 1 && v->n <= 1024) cpu_pct = v->n * 100;
        else if (v->kind == MESH_TOML_FLOAT && v->f >= 0.01 && v->f <= 1024.0) cpu_pct = (uint32_t)round(v->f * 100.0);
        else {
            m_(p, "cpu: cores, 0.01..=1024 (e.g. 2.0), got ");
            v_(p, v);
            return fail_(p, err);
        }
    }
    if (!num(p, "nice", 0, &m->nice, err)) return false;
    if (m->nice > 19) FAIL("nice %u: 0..=19 (a worker never runs above meshd's priority)", (unsigned)m->nice);

    /* pipes */
    v = get(p, "inbox");
    if (!v) {
        if (get(p, "inbox_type")) FAIL("inbox_type: only with inbox");
    } else if (v->kind == MESH_TOML_STR) {
        const char *is = v->s;
        uint32_t parts[3];
        size_t np = 0;
        bool ok = true;
        for (const char *c = is;;) {
            const char *e = strchr(c, ':');
            size_t n = e ? (size_t)(e - c) : strlen(c);
            uint32_t x;
            if (!ints(c, n, &x)) { ok = false; break; }
            if (np < 3) parts[np] = x;
            np++;
            if (!e) break;
            c = e + 1;
        }
        mesh_pipe_inbox ib = {0, ok && np == 3 ? parts[1] : 0, ok && np == 3 ? parts[2] : 0};
        if (!ok || np != 3 || !mesh_pipe_inbox_valid(&ib)) {
            m_(p, "inbox ");
            q_(p, is);
            FAIL(": \"off:cap:rec\" (cap records, a power of two 4..=%u: half per producer side; rec bytes, a multiple of 4, 4..=%u)",
                 (unsigned)MESH_PIPE_CAP_MAX, (unsigned)MESH_PIPE_REC_MAX);
        }
        uint32_t off = parts[0], icap = parts[1], rec = parts[2];
        if (off % 0x80 != 0) {
            m_(p, "inbox ");
            q_(p, is);
            FAIL(": offset 0x%x not 0x80-aligned (its head and tail lines)", (unsigned)off);
        }
        uint32_t ibytes = (uint32_t)mesh_pipe_inbox_bytes(icap, rec);
        if (!in_view(p, "inbox", off, ibytes, err)) return false;
        span ibs = {off, ibytes, "inbox"};
        span others[3];
        size_t no = 0;
        if (m->input_len != 0) others[no++] = (span){m->input_off, (uint32_t)MESH_IQ_DATA + m->input_len, "input"};
        if (m->has_request) {
            others[no++] = (span){m->request.req_off, m->request.req_max, "request"};
            others[no++] = (span){m->request.resp_off, m->request.resp_max, "request response"};
        }
        for (size_t i = 0; i < no; i++)
            if (overlaps(ibs, others[i])) {
                m_(p, "inbox ");
                q_(p, is);
                FAIL(" overlaps %s 0x%x+0x%x", others[i].what, (unsigned)others[i].off, (unsigned)others[i].len);
            }
        const char *ty;
        if (!text(p, "inbox_type", "", &ty, err)) return false;
        if (!valid_type(ty)) {
            m_(p, "inbox_type ");
            q_(p, ty);
            FAIL(": required with inbox, [a-z0-9_]{1,%d}", MESH_NAME_MAX);
        }
        m->inbox = mesh_alloc(sizeof *m->inbox);
        *m->inbox = (mesh_manifest_inbox){off, icap, rec, mesh_strdup(ty)};
    } else {
        m_(p, "inbox: expected \"off:cap:rec\", got ");
        v_(p, v);
        return fail_(p, err);
    }
    if (!list(p, "feeds", &items, &nitems, err)) return false;
    for (size_t i = 0; i < nitems; i++) {
        const char *f = items[i];
        const char *c = strchr(f, ':');
        if (!c) {
            m_(p, "feeds ");
            q_(p, f);
            FAIL(": \"consumer:type\"");
        }
        char *consumer = mesh_strndup(f, (size_t)(c - f));
        const char *t = c + 1;
        if (!vname(consumer) || strcmp(consumer, m->name) == 0) {
            free(consumer);
            m_(p, "feeds ");
            q_(p, f);
            FAIL(": another program's name");
        }
        if (!valid_type(t)) {
            free(consumer);
            m_(p, "feeds ");
            q_(p, f);
            FAIL(": type [a-z0-9_]{1,%d}", MESH_NAME_MAX);
        }
        for (size_t j = 0; j < m->nfeeds; j++)
            if (strcmp(m->feeds[j].consumer, consumer) == 0) {
                m_(p, "feeds: ");
                q_(p, consumer);
                free(consumer);
                FAIL(" twice");
            }
        m->feeds = mesh_realloc(m->feeds, (m->nfeeds + 1) * sizeof *m->feeds);
        m->feeds[m->nfeeds++] = (mesh_manifest_feed){consumer, mesh_strdup(t)};
    }
    if (lockstep && (m->inbox || m->nfeeds))
        FAIL("inbox, feeds: not for the lockstep program (its steps must not depend on another program's timing)");
    v = get(p, "background");
    if (v && v->kind != MESH_TOML_BOOL) {
        m_(p, "background: expected true/false, got ");
        v_(p, v);
        return fail_(p, err);
    }
    m->background = v && v->b;
    if (!text(p, "device", "cpu", &s, err)) return false;
    if (strcmp(s, "cpu") == 0) m->device_gpu = false;
    else if (strcmp(s, "gpu") == 0) m->device_gpu = true;
    else {
        m_(p, "device ");
        q_(p, s);
        FAIL(": \"cpu\" or \"gpu\"");
    }

    /* jobs */
    if (m->policy == MESH_POLICY_JOB) {
        if (lockstep) FAIL("policy \"job\": not with peer = \"lockstep\" (a job's items go to any member, a group's steps to all)");
        if (m->place) FAIL("policy \"job\": not with place (a job's items are leased, not placed)");
        if (m->arg_mode != MESH_ARG_INDEX) FAIL("policy \"job\": arg is the item (OP_JOB), not \"view_u32\"");
    } else {
        static const char *const jk[] = {"job_base", "job_out", "job_payload"};
        for (size_t i = 0; i < 3; i++)
            if (get(p, jk[i])) FAIL("%s: only for policy = \"job\"", jk[i]);
    }
    v = get(p, "job_base");
    if (v) {
        if (v->kind == MESH_TOML_INT && v->n % 8 == 0) {
            if (!in_view(p, "job_base", v->n, 8, err)) return false;
            m->has_job_base = true;
            m->job_base = v->n;
        } else {
            m_(p, "job_base: an 8-aligned view offset (of the u64 item base), got ");
            v_(p, v);
            return fail_(p, err);
        }
    }
    v = get(p, "job_out");
    if (v) {
        if (v->kind != MESH_TOML_STR) {
            m_(p, "job_out: expected \"off:rec\", got ");
            v_(p, v);
            return fail_(p, err);
        }
        uint32_t off, rec;
        if (!pair(v->s, &off, &rec)) {
            m_(p, "job_out ");
            q_(p, v->s);
            FAIL(": \"off:rec\"");
        }
        if (off % 4 != 0 || rec % 4 != 0 || !(rec >= 4 && rec <= MESH_JOB_OUT_MAX)) {
            m_(p, "job_out ");
            q_(p, v->s);
            FAIL(": offset 4-aligned, rec a multiple of 4 in 4..=%u", (unsigned)MESH_JOB_OUT_MAX);
        }
        if (!in_view(p, "job_out", off, rec * MESH_PROG_RESULTS, err)) return false;
        if (m->has_job_base && m->job_base < off + rec * MESH_PROG_RESULTS && off < m->job_base + 8) {
            m_(p, "job_out ");
            q_(p, v->s);
            FAIL(" overlaps job_base 0x%x", (unsigned)m->job_base);
        }
        m->has_job_out = true;
        m->job_out_off = off;
        m->job_out_rec = rec;
    }
    v = get(p, "job_payload");
    if (v) {
        if (v->kind != MESH_TOML_STR) {
            m_(p, "job_payload: expected \"off:len\", got ");
            v_(p, v);
            return fail_(p, err);
        }
        uint32_t off, len;
        if (!pair(v->s, &off, &len)) {
            m_(p, "job_payload ");
            q_(p, v->s);
            FAIL(": \"off:len\"");
        }
        if (off % 4 != 0 || len % 4 != 0 || !(len >= 4 && len <= MESH_JOB_PAYLOAD_MAX)) {
            m_(p, "job_payload ");
            q_(p, v->s);
            FAIL(": offset 4-aligned, len a multiple of 4 in 4..=%u", (unsigned)MESH_JOB_PAYLOAD_MAX);
        }
        uint32_t total = len * MESH_PROG_RESULTS;
        if (!in_view(p, "job_payload", off, total, err)) return false;
        bool over_base = m->has_job_base && m->job_base < off + total && off < m->job_base + 8;
        bool over_out = m->has_job_out && m->job_out_off < off + total &&
                        off < m->job_out_off + m->job_out_rec * MESH_PROG_RESULTS;
        if (over_base || over_out) {
            m_(p, "job_payload ");
            q_(p, v->s);
            FAIL(" overlaps job_base or job_out");
        }
        m->has_job_payload = true;
        m->job_payload_off = off;
        m->job_payload_len = len;
    }

    /* the fleet scheduler's keys */
    mesh_needs *nd = &m->needs;
    v = get(p, "replicas");
    if (!v || (v->kind == MESH_TOML_STR && strcmp(v->s, "all") == 0)) {
        nd->replicas.all = true;
    } else if (v->kind == MESH_TOML_INT && v->n >= 1 && v->n <= MESH_MAX_SYSTEMS) {
        nd->replicas.all = false;
        nd->replicas.n = v->n;
    } else {
        m_(p, "replicas: \"all\" or 1..=%u, got ", (unsigned)MESH_MAX_SYSTEMS);
        v_(p, v);
        return fail_(p, err);
    }
    if (lockstep && get(p, "replicas")) FAIL("replicas: not for the lockstep program (its group: group_size, group_members)");
    if (cap != MESH_PLACE_CAP_NONE && !nd->replicas.all)
        FAIL("replicas: not with `place` (a placed program is loaded on every member; `place` decides who runs it)");
    if (!text(p, "spread", "spread", &s, err)) return false;
    if (strcmp(s, "spread") == 0) nd->pack = false;
    else if (strcmp(s, "pack") == 0) nd->pack = true;
    else {
        m_(p, "spread ");
        q_(p, s);
        FAIL(": \"spread\" or \"pack\"");
    }
    if (!term_list(p, "prefer", &nd->prefer, &nd->nprefer, err)) return false;
    if (!term_list(p, "avoid", &nd->avoid, &nd->navoid, err)) return false;
    v = get(p, "arch");
    if (v) {
        if (v->kind == MESH_TOML_STR && v->s[0]) nd->arch = mesh_strdup(v->s);
        else {
            m_(p, "arch: a CPU architecture (\"aarch64\", \"x86_64\"), got ");
            v_(p, v);
            return fail_(p, err);
        }
    }
    if (!num(p, "min_mem_mb", 0, &nd->min_mem_mb, err)) return false;
    v = get(p, "colocate_with");
    if (v) {
        if (v->kind == MESH_TOML_STR && vname(v->s) && strcmp(v->s, m->name) != 0) nd->colocate_with = mesh_strdup(v->s);
        else {
            m_(p, "colocate_with: another program's name, got ");
            v_(p, v);
            return fail_(p, err);
        }
    }
    if (nd->colocate_with && (lockstep || get(p, "replicas")))
        FAIL("colocate_with: it runs where that program runs (no replicas, not lockstep)");
    v = get(p, "group");
    if (v) {
        if (v->kind == MESH_TOML_STR && vname(v->s)) nd->group = mesh_strdup(v->s);
        else {
            m_(p, "group: a name, got ");
            v_(p, v);
            return fail_(p, err);
        }
    }
    if (!lockstep && (get(p, "group") || get(p, "group_size") || get(p, "group_members")))
        FAIL("group, group_size, group_members: only with peer = \"lockstep\"");
    if (!num(p, "group_size", 0, &nd->group_size, err)) return false;
    if (nd->group_size > MESH_MAX_SYSTEMS)
        FAIL("group_size %u: 0 (every eligible member) ..=%u", (unsigned)nd->group_size, (unsigned)MESH_MAX_SYSTEMS);
    if (!list(p, "group_members", &items, &nitems, err)) return false;
    if (!(nitems == 1 && strcmp(items[0], "all") == 0))
        if (!term_list(p, "group_members", &nd->group_members, &nd->ngroup_members, err)) return false;
    v = get(p, "priority");
    if (!v) nd->priority = 0;
    else if (v->kind == MESH_TOML_INT && v->n <= 10) nd->priority = (int32_t)v->n;
    else if (v->kind == MESH_TOML_NEG && v->n <= 10) nd->priority = -(int32_t)v->n;
    else {
        m_(p, "priority: -10..=10, got ");
        v_(p, v);
        return fail_(p, err);
    }
    if (!text(p, "migrate", "state", &s, err)) return false;
    if (strcmp(s, "state") == 0) nd->migrate = MESH_MIGRATE_STATE;
    else if (strcmp(s, "restart") == 0) nd->migrate = MESH_MIGRATE_RESTART;
    else if (strcmp(s, "never") == 0) nd->migrate = MESH_MIGRATE_NEVER;
    else {
        m_(p, "migrate ");
        q_(p, s);
        FAIL(": \"state\", \"restart\" or \"never\"");
    }
    nd->backends = mesh_strs_copy(m->backends, m->nbackends);
    nd->nbackends = m->nbackends;
    nd->feeds = m->nfeeds ? mesh_alloc(m->nfeeds * sizeof *nd->feeds) : NULL;
    for (size_t i = 0; i < m->nfeeds; i++) nd->feeds[i] = mesh_strdup(m->feeds[i].consumer);
    nd->nfeeds = m->nfeeds;
    nd->cpu = (float)cpu_pct / 100.0f;
    nd->mem_mb = native ? m->mem_mb : wasm ? m->mem_mb * m->nodes : 0;
    nd->lockstep = lockstep;
    nd->view_bytes = m->view_bytes;
    nd->warps = m->warps;
    nd->shared = m->shared_bytes;
    if (!num(p, "view_abi", 1, &m->view_abi, err)) return false;
    m->cpu_pct = native ? cpu_pct : 0;
    return true;
}

bool mesh_manifest_parse(const char *src, size_t n, mesh_manifest *out, char **err) {
    P p = {0};
    *out = (mesh_manifest){0};
    bool ok = parse(&p, src, n, out, err);
    for (size_t i = 0; i < p.nkv; i++) {
        free(p.kvs[i].key);
        mesh_toml_val_free(&p.kvs[i].val);
    }
    free(p.kvs);
    mesh_text_free(&p.msg);
    if (!ok) mesh_manifest_free(out);
    return ok;
}
