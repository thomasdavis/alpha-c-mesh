/* fleet.rs Persist: encode and decode. */
#include "mesh/fleet_persist.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

mfp_persist mfp_clone(const mfp_persist *p) {
    return (mfp_persist){p->term, p->has_voted, p->voted, mfd_doc_clone(&p->accepted), mfd_doc_clone(&p->committed)};
}

bool mfp_eq(const mfp_persist *a, const mfp_persist *b) {
    return a->term == b->term && a->has_voted == b->has_voted && (!a->has_voted || a->voted == b->voted) && mfd_doc_eq(&a->accepted, &b->accepted) &&
           mfd_doc_eq(&a->committed, &b->committed);
}

void mfp_free(mfp_persist *p) {
    mfd_doc_free(&p->accepted);
    mfd_doc_free(&p->committed);
    *p = (mfp_persist){0};
}

char *mfp_encode(const mfp_persist *p, uint32_t n) {
    char *a = mfd_encode(&p->accepted), *c = mfd_encode(&p->committed);
    char voted[16];
    if (p->has_voted) snprintf(voted, sizeof voted, "%u", p->voted);
    else strcpy(voted, "-");
    char *s = mt_fmt("# meshd fleet state: term, vote, the accepted and committed desired state\nsystems %u\nterm %llu\nvoted %s\naccepted %zu\n%scommitted %zu\n%s", n,
                     (unsigned long long)p->term, voted, strlen(a), a, strlen(c), c);
    free(a);
    free(c);
    return s;
}

static char *bad(const char *line) {
    char *q = rf_debug_str(line);
    char *e = mt_fmt("bad line %s", q);
    free(q);
    return e;
}

bool mfp_decode(const char *text, uint32_t n, mfp_persist *out, char **err) {
    mfp_persist p = {0};
    const char *rest = text;
    int docs = 0;
    while (*rest) {
        const char *nl = strchr(rest, '\n');
        char *line = nl ? mt_strndup(rest, (size_t)(nl - rest)) : mt_strdup(rest);
        rest = nl ? nl + 1 : rest + strlen(rest);
        mt_strs w = mt_words(line);
        bool ok = true;
        uint64_t u;
        if (w.n == 0 || w.v[0][0] == '#') {
        } else if (w.n == 2 && strcmp(w.v[0], "systems") == 0) {
            uint32_t s;
            if (!mp_u32(w.v[1], &s)) ok = false, *err = bad(line);
            else if (s != n) ok = false, *err = mt_fmt("written for a cluster of %u, this one has %u (remove the file to start the fleet afresh)", s, n);
        } else if (w.n == 2 && strcmp(w.v[0], "term") == 0) {
            if (!mp_u64(w.v[1], &p.term)) ok = false, *err = bad(line);
        } else if (w.n == 2 && strcmp(w.v[0], "voted") == 0) {
            if (strcmp(w.v[1], "-") == 0) p.has_voted = false;
            else if (mp_u32(w.v[1], &p.voted)) p.has_voted = true;
            else ok = false, *err = bad(line);
        } else if (w.n == 2 && (strcmp(w.v[0], "accepted") == 0 || strcmp(w.v[0], "committed") == 0)) {
            bool acc = strcmp(w.v[0], "accepted") == 0;
            if (!mp_u64(w.v[1], &u)) ok = false, *err = bad(line);
            else if (strlen(rest) < u) ok = false, *err = mt_fmt("%s: truncated", w.v[0]);
            else {
                char *t = mt_strndup(rest, (size_t)u);
                mfd_doc d;
                if (mfd_decode(t, &d, err)) {
                    mfd_doc *slot = acc ? &p.accepted : &p.committed;
                    mfd_doc_free(slot);
                    *slot = d;
                    rest += u;
                    docs++;
                } else ok = false;
                free(t);
            }
        } else {
            ok = false;
            *err = bad(line);
        }
        mt_strs_free(&w);
        free(line);
        if (!ok) {
            mfp_free(&p);
            return false;
        }
    }
    if (docs != 2) {
        *err = mt_strdup("incomplete (no accepted and committed documents)");
        mfp_free(&p);
        return false;
    }
    *out = p;
    return true;
}
