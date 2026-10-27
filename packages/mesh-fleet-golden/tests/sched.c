/* 150 random scheduler scenarios from the Rust: members, weights, measured performance and a document
 * in; the placement pass's document, changes and explains, every member's filter or score for every
 * program, then twelve ticks of the owner policy, the rebalancer, group proposals and moves out --
 * each compared with what the Rust computed. */
#include "golden.h"
#include "mesh/sched_place.h"
#include "mesh/sched_tracker.h"

typedef struct {
    const char *p;
} cursor;

/* The next line (without its newline), or NULL at the end. */
static char *next_line(cursor *c) {
    if (!*c->p) return NULL;
    const char *e = strchr(c->p, '\n');
    size_t n = e ? (size_t)(e - c->p) : strlen(c->p);
    char *l = mt_strndup(c->p, n);
    c->p += n + (e ? 1 : 0);
    return l;
}

static char *next_block(cursor *c, size_t n) {
    char *b = mt_strndup(c->p, n);
    c->p += n;
    return b;
}

static mof32 opt(const char *s) { return strcmp(s, "-") == 0 ? mof32_none() : mof32_some(f32_bits(s)); }

static msm_member parse_member(mt_strs *w) {
    char **v = w->v;
    msm_member m = msm_member_default();
    m.id = (uint32_t)atoi(v[1]);
    m.up = atoi(v[2]);
    m.down_ms = strtoull(v[3], NULL, 10);
    m.cordoned = atoi(v[4]);
    MSM_SET(m.host, v[5]);
    MSM_SET(m.gpu, v[6]);
    MSM_SET(m.arch, v[7]);
    m.cc = (uint32_t)atoi(v[8]), m.sms = (uint32_t)atoi(v[9]), m.blocks_per_sm = (uint32_t)atoi(v[10]);
    m.max_threads = (uint32_t)atoi(v[11]), m.max_shared = (uint32_t)atoi(v[12]);
    m.cpus = (uint32_t)atoi(v[13]), m.cores_big = (uint32_t)atoi(v[14]), m.cores_little = (uint32_t)atoi(v[15]);
    m.ram_mb = (uint32_t)atoi(v[16]), m.mem_avail_mb = (uint32_t)atoi(v[17]), m.disk_free_mb = (uint32_t)atoi(v[18]);
    m.gpu_share = opt(v[19]);
    m.cpu_util = f32_bits(v[20]), m.cpu_big = f32_bits(v[21]), m.cpu_little = f32_bits(v[22]);
    m.psi_cpu = opt(v[23]), m.psi_mem = opt(v[24]);
    m.meshd_cpu = f32_bits(v[25]);
    m.load_known = atoi(v[26]);
    m.heap_kb = (uint32_t)atoi(v[27]), m.slots_free = (uint32_t)atoi(v[28]);
    m.nrtt = (size_t)atoi(v[29]);
    size_t i = 30;
    for (size_t k = 0; k < m.nrtt; k++) m.rtt_us[k] = opt(v[i++]);
    if (strcmp(v[i], "-") != 0) m.busy = unhex(v[i]);
    i++;
    size_t nf = (size_t)atoi(v[i++]);
    for (size_t k = 0; k < nf; k++, i += 2) {
        char *why = unhex(v[i + 1]);
        ms_set(&m.fails, v[i], why, true);
        free(why);
    }
    return m;
}

static msw_weights parse_weights(mt_strs *w) {
    char **v = w->v;
    msw_weights x = {.perf = f64_bits(v[1]), .load = f64_bits(v[2]), .mem = f64_bits(v[3]), .spread = f64_bits(v[4]), .rtt = f64_bits(v[5]), .prefer = f64_bits(v[6]),
                     .sticky = f64_bits(v[7]), .owner = f64_bits(v[8]), .improve = f64_bits(v[9]), .hold_ms = strtoull(v[10], NULL, 10),
                     .dwell_ms = strtoull(v[11], NULL, 10), .per_min = (uint32_t)atoi(v[12]), .replace_ms = strtoull(v[13], NULL, 10),
                     .owner_share = f32_bits(v[14]), .owner_psi = f32_bits(v[15]), .owner_ms = strtoull(v[16], NULL, 10), .tick_ms = strtoull(v[17], NULL, 10),
                     .reserve = f32_bits(v[18])};
    return x;
}

/* The next line, split; must start with tag. */
static mt_strs expect_words(cursor *c, const char *tag, int seed) {
    char *l = next_line(c);
    mt_strs w = mt_words(l ? l : "");
    if (!w.n || strcmp(w.v[0], tag) != 0) {
        fprintf(stderr, "case %d: wanted %s, got %s\n", seed, tag, l ? l : "(end)");
        exit(1);
    }
    free(l);
    return w;
}

static bool expect_hashed(cursor *c, const char *text, int seed, const char *what) {
    char *l = next_line(c);
    mt_strs w = mt_words(l);
    bool ok = w.n >= 2 && matches(text, w.v[w.n - 2], w.v[w.n - 1]);
    if (!ok) fprintf(stderr, "case %d: %s differs from the Rust: C wrote %s\n", seed, what, text);
    mt_strs_free(&w);
    free(l);
    return CHECK(ok);
}

static void run_case(cursor *c, mt_strs *head) {
    int seed = atoi(head->v[1]);
    size_t n = (size_t)atoi(head->v[2]);
    mt_strs w = expect_words(c, "w", seed);
    msw_weights wt = parse_weights(&w);
    mt_strs_free(&w);
    msm_member ms[8];
    for (size_t k = 0; k < n; k++) {
        w = expect_words(c, "m", seed);
        ms[k] = parse_member(&w);
        mt_strs_free(&w);
    }
    msm_perfs perf = {0};
    mfd_doc doc;
    for (;;) {
        char *l = next_line(c);
        w = mt_words(l);
        free(l);
        if (!strcmp(w.v[0], "p")) {
            msm_perf p = {.hash = strtoull(w.v[3], NULL, 16), .run = atoi(w.v[5]), .task_us = opt(w.v[6]), .tasks_s = f32_bits(w.v[7]), .cpu = f32_bits(w.v[8]),
                          .share = opt(w.v[9]), .rss_mb = f32_bits(w.v[10])};
            msm_set_str(p.exec, sizeof p.exec, w.v[4]);
            msm_perfs_set(&perf, w.v[1], (uint32_t)atoi(w.v[2]), &p);
            mt_strs_free(&w);
            continue;
        }
        char *t = next_block(c, (size_t)atoi(w.v[1]));
        char *err = NULL;
        if (!mfd_decode(t, &doc, &err)) {
            fprintf(stderr, "case %d: %s\n", seed, err);
            exit(1);
        }
        free(t);
        mt_strs_free(&w);
        break;
    }
    msp_input in = {&doc, ms, n, &perf, &wt};
    mt_strs ch;
    msp_explains ex;
    mfd_doc placed = msp_place(&in, &ch, &ex);
    char *pt = mfd_encode(&placed);
    expect_hashed(c, pt, seed, "the placed document");
    free(pt);
    w = expect_words(c, "=changes", seed);
    size_t nch = (size_t)atoi(w.v[1]);
    mt_strs_free(&w);
    CHECK_U(ch.n, nch);
    for (size_t i = 0; i < nch; i++) expect_hashed(c, i < ch.n ? ch.v[i] : "", seed, "a change");
    /* explains, sorted by program name (a BTreeMap) */
    for (size_t i = 0; i < doc.nprogs; i++) {
        const msp_explain *e = msp_explains_get(&ex, doc.pname[i]);
        char *et = msp_explain_text(e);
        expect_hashed(c, et, seed, "an explain");
        free(et);
    }
    /* filter / score of every program on every member */
    for (size_t k = 0; k < n; k++)
        for (size_t i = 0; i < doc.nprogs; i++) {
            char *l = next_line(c);
            w = mt_words(l);
            free(l);
            char exec[16], *err = NULL;
            const mfd_prog *p = &doc.prog[i];
            if (msc_filter(&ms[k], &p->needs, exec, &err)) {
                CHECK(!strcmp(w.v[0], "=score") && !strcmp(w.v[3], exec));
                msc_cx cx = msc_cx_simple(&wt, msm_perfs_get(&perf, doc.pname[i], ms[k].id));
                msc_score_t s = msc_score_terms(&ms[k], &p->needs, &cx);
                if (!CHECK(s.total == f64_bits(w.v[4]))) fprintf(stderr, "case %d: score of %s on %zu\n", seed, doc.pname[i], k);
                char *x = msc_explain(&s);
                expect_hashed(c, x, seed, "a score's explain");
                free(x);
            } else {
                CHECK(!strcmp(w.v[0], "=filter"));
                expect_hashed(c, err, seed, "a filter's reason");
                free(err);
            }
            mt_strs_free(&w);
        }
    /* the rebalancer over time */
    mst_tracker *t = mst_new();
    mfd_doc d = mfd_doc_clone(&placed);
    for (int step = 0; step < 12; step++) {
        w = expect_words(c, "=tick", seed);
        uint64_t now = strtoull(w.v[2], NULL, 10);
        bool busy = atoi(w.v[3]);
        mt_strs_free(&w);
        char **busy_lines = mt_alloc(n * sizeof *busy_lines);
        for (size_t k = 0; k < n; k++) {
            char *l = next_line(c);
            mt_strs v = mt_words(l);
            free(l);
            ms[k].gpu_share = opt(v.v[1]);
            ms[k].psi_cpu = opt(v.v[2]);
            ms[k].cpu_util = f32_bits(v.v[3]);
            mt_strs_free(&v);
            busy_lines[k] = next_line(c);
        }
        mst_owner(t, ms, n, now, &wt);
        for (size_t k = 0; k < n; k++) {
            mt_strs v = mt_words(busy_lines[k]);
            if (!strcmp(v.v[1], "-")) CHECK(ms[k].busy == NULL);
            else if (!CHECK(ms[k].busy && matches(ms[k].busy, v.v[1], v.v[2]))) fprintf(stderr, "case %d: busy of %zu: %s\n", seed, k, ms[k].busy ? ms[k].busy : "-");
            mt_strs_free(&v);
            free(busy_lines[k]);
        }
        free(busy_lines);
        msp_input ti = {&d, ms, n, &perf, &wt};
        mst_move mv;
        bool moved = mst_step(t, &ti, now, busy, &mv);
        mt_strs notes = mt_strs_clone(mst_notes(t));
        char *g = NULL, *gwhy = NULL;
        ml_list gm = {0};
        bool reform = mst_group_step(t, &ti, now, &g, &gm, &gwhy);
        char *l = next_line(c);
        w = mt_words(l);
        free(l);
        if (moved) {
            char *progs = mt_join(&mv.programs, ",");
            if (!CHECK(!strcmp(w.v[0], "=move") && !strcmp(w.v[1], progs) && (uint32_t)atoi(w.v[2]) == mv.from && (uint32_t)atoi(w.v[3]) == mv.to &&
                       atoi(w.v[4]) == mv.state && atoi(w.v[5]) == mv.forced))
                fprintf(stderr, "case %d tick %d: moved %s %u -> %u, Rust: %s\n", seed, step, progs, mv.from, mv.to, w.v[0]);
            free(progs);
            if (!strcmp(w.v[0], "=move")) expect_hashed(c, mv.why, seed, "a move's why");
        } else if (!CHECK(!strcmp(w.v[0], "=nomove"))) {
            fprintf(stderr, "case %d tick %d: no move, Rust moved %s\n", seed, step, w.v[1]);
            free(next_line(c));
        }
        mt_strs_free(&w);
        if (moved) {
            mst_moved(t, &mv, now);
            mfd_doc s = mst_start_move(&d, &mv, d.version + 1);
            mfd_doc_free(&d);
            d = s;
            w = expect_words(c, "=end", seed);
            if (atoi(w.v[1])) {
                mfd_doc e = mst_end_move(&d, &mv, atoi(w.v[2]), "done");
                mfd_doc_free(&d);
                d = e;
            }
            mt_strs_free(&w);
            mst_move_free(&mv);
        }
        w = expect_words(c, "=notes", seed);
        size_t nn = (size_t)atoi(w.v[1]);
        mt_strs_free(&w);
        if (!CHECK_U(notes.n, nn))
            for (size_t i = 0; i < notes.n; i++) fprintf(stderr, "  note: %s\n", notes.v[i]);
        for (size_t i = 0; i < nn; i++) expect_hashed(c, i < notes.n ? notes.v[i] : "", seed, "a note");
        mt_strs_free(&notes);
        l = next_line(c);
        w = mt_words(l);
        free(l);
        if (reform) {
            char *lt = ml_text(&gm);
            CHECK(!strcmp(w.v[0], "=reform") && !strcmp(w.v[1], g) && !strcmp(w.v[2], lt));
            free(lt);
            if (!strcmp(w.v[0], "=reform")) expect_hashed(c, gwhy, seed, "a reform's why");
        } else if (!CHECK(!strcmp(w.v[0], "=noreform"))) free(next_line(c));
        mt_strs_free(&w);
        free(g), free(gwhy), ml_free(&gm);
        char *dt = mfd_encode(&d);
        expect_hashed(c, dt, seed, "the document after the tick");
        free(dt);
    }
    w = expect_words(c, "end", seed);
    mt_strs_free(&w);
    mst_free(t);
    mfd_doc_free(&d);
    mt_strs_free(&ch);
    msp_explains_free(&ex);
    mfd_doc_free(&placed);
    mfd_doc_free(&doc);
    msm_perfs_free(&perf);
    for (size_t k = 0; k < n; k++) msm_member_free(&ms[k]);
}

int main(void) {
    size_t len;
    char *text = slurp("data/sched.txt", &len);
    cursor c = {text};
    int cases = 0;
    for (;;) {
        char *l = next_line(&c);
        if (!l) break;
        mt_strs head = mt_words(l);
        free(l);
        if (head.n == 3 && !strcmp(head.v[0], "case")) {
            run_case(&c, &head);
            cases++;
        }
        mt_strs_free(&head);
    }
    CHECK_I(cases, 150);
    free(text);
    return mesh_check_done();
}
