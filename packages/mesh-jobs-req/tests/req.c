/* Control lines parsed as the Rust parses them: every answer (request or error text) matches. */
#include "mesh/jobs/req.h"
#include "mesh/check.h"
#include "mesh/strbuf.h"
#include "gold_req.h"

#include <stdlib.h>
#include <string.h>

static char *canon(const mj_req *r) {
    msb b = {0};
    switch (r->kind) {
    case MJ_REQ_SUBMIT: {
        const mj_submit *s = &r->submit;
        msb_printf(&b, "submit members %u program %s op %u items ", s->members, s->program, s->op);
        mj_ranges_word(&s->items, &b);
        msb_printf(&b, " priority %d attempts %u deadline %llu mode %s setup ", s->priority, s->attempts, (unsigned long long)s->deadline_s,
                   mj_mode_word(s->mode));
        for (size_t i = 0; i < s->nsetup; i++) msb_printf(&b, "%s%u:%u", i ? "," : "", s->setup[i].op, s->setup[i].arg);
        break;
    }
    case MJ_REQ_CANCEL: msb_printf(&b, "cancel %llu", (unsigned long long)r->job); break;
    case MJ_REQ_FORGET: msb_printf(&b, "forget %llu", (unsigned long long)r->job); break;
    case MJ_REQ_TONE: msb_printf(&b, "tone %llu %u", (unsigned long long)r->job, r->tone); break;
    }
    return msb_take(&b);
}

int main(void) {
    for (size_t i = 0; i < sizeof GOLD / sizeof *GOLD; i++) {
        char *line = mstrdup(GOLD[i][0]);
        const char *w[64];
        size_t n = 0;
        for (char *t = strtok(line, " "); t && n < 64; t = strtok(NULL, " ")) w[n++] = t;
        mj_req r;
        char *err = NULL;
        char *got;
        if (mj_req_parse(w, n, &r, &err)) {
            got = canon(&r);
            mj_req c = mj_req_clone(&r);
            mj_req_free(&c);
            mj_req_free(&r);
        } else {
            got = mfmt("error %s", err);
            free(err);
        }
        if (!CHECK_S(got, GOLD[i][1])) fprintf(stderr, "  for: %s\n", GOLD[i][0]);
        free(got);
        free(line);
    }
    return mesh_check_done();
}
