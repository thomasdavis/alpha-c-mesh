/* The fleet core over the Rust tests' simulated network, 80 seeds, against the Rust's own run: the
 * same RNG and the same core make the same trajectory, so every leadership (term: member), every
 * committed version (and its document's hash), the fail-stops, the clock and each member's final
 * (term, role, committed version) must be the Rust's exactly. */
#include "golden.h"
#include "sim.h"

static int cmp_u64pair(const void *a, const void *b) {
    const uint64_t *x = a, *y = b;
    return x[0] < y[0] ? -1 : x[0] > y[0];
}

int main(void) {
    size_t len;
    char *text = slurp("data/sim.txt", &len);
    mt_strs lines = mt_lines(text);
    int seeds = 0;
    for (size_t li = 0; li + 3 < lines.n; li += 4) {
        mt_strs h = mt_words(lines.v[li]);
        uint64_t seed = strtoull(h.v[1], NULL, 10);
        uint32_t n = (uint32_t)atoi(h.v[2]);
        sim_t s;
        sim_init(&s, n, seed * 7919, seed % 4 * 5);
        char *err = NULL;
        for (int round = 0; round < 40; round++) {
            uint64_t r = sim_rand(&s, 10);
            if (r == 0 || r == 1) {
                for (uint32_t k = 0; k < n; k++) s.side[k] = (uint32_t)sim_rand(&s, 2);
                sim_relink(&s);
            } else if (r == 2) {
                for (uint32_t k = 0; k < n; k++) s.side[k] = 0;
                sim_relink(&s);
            } else if (r == 9 && sim_rand(&s, 3) == 0) {
                s.bad_disk[sim_rand(&s, n)] = true;
            } else if (r == 3) {
                uint32_t k = (uint32_t)sim_rand(&s, n), live = 0;
                for (uint32_t j = 0; j < n; j++) live += s.cores[j] != NULL;
                if (s.cores[k] && live > 1) sim_crash(&s, k);
            } else if (r == 4) {
                uint32_t k = (uint32_t)sim_rand(&s, n);
                if (!s.cores[k]) sim_restart(&s, k);
            } else {
                int l = sim_leader(&s);
                if (l >= 0) {
                    char tag[16];
                    snprintf(tag, sizeof tag, "r%d", round);
                    if (!sim_propose(&s, (uint32_t)l, tag, &err)) free(err);
                }
            }
            sim_step(&s, 200 + sim_rand(&s, 3000));
        }
        for (uint32_t k = 0; k < n; k++) {
            s.side[k] = 0;
            s.bad_disk[k] = false;
            if (!s.cores[k]) sim_restart(&s, k);
        }
        sim_relink(&s);
        s.loss = 0;
        sim_step(&s, 8000);
        int l = sim_leader(&s);
        uint64_t v = l >= 0 ? sim_propose(&s, (uint32_t)l, "final", &err) : 0;
        sim_step(&s, 2000);
        char *want = mt_fmt("seed %llu %u stops %u final %llu now %llu", (unsigned long long)seed, n, s.fail_stops, (unsigned long long)v, (unsigned long long)s.now);
        if (!CHECK_S(want, lines.v[li])) fprintf(stderr, "  seed %llu diverged\n", (unsigned long long)seed);
        free(want);
        /* leaders: (term, member), unique by term (a BTreeMap), sorted */
        uint64_t (*lt)[2] = mt_alloc((s.nleaders + 1) * sizeof *lt);
        size_t nl = 0;
        for (size_t i = 0; i < s.nleaders; i++) {
            size_t j = 0;
            for (; j < nl; j++)
                if (lt[j][0] == s.leader_term[i]) break;
            lt[j][0] = s.leader_term[i], lt[j][1] = s.leader_of[i];
            if (j == nl) nl++;
        }
        qsort(lt, nl, sizeof *lt, cmp_u64pair);
        mt_buf b = {0};
        mt_printf(&b, "leaders %zu", nl);
        for (size_t i = 0; i < nl; i++) mt_printf(&b, " %llu:%llu", (unsigned long long)lt[i][0], (unsigned long long)lt[i][1]);
        CHECK_S(mt_str(&b), lines.v[li + 1]);
        mt_buf_free(&b);
        free(lt);
        /* committed: by version */
        uint64_t (*cv)[2] = mt_alloc((s.ncommitted + 1) * sizeof *cv);
        for (size_t i = 0; i < s.ncommitted; i++) {
            char *t = mfd_encode(&s.committed[i]);
            cv[i][0] = s.committed[i].version;
            cv[i][1] = mesh_fnv1a64((const uint8_t *)t, strlen(t));
            free(t);
        }
        qsort(cv, s.ncommitted, sizeof *cv, cmp_u64pair);
        mt_printf(&b, "committed %zu", s.ncommitted);
        for (size_t i = 0; i < s.ncommitted; i++) mt_printf(&b, " %llu:%016llx", (unsigned long long)cv[i][0], (unsigned long long)cv[i][1]);
        CHECK_S(mt_str(&b), lines.v[li + 2]);
        mt_buf_free(&b);
        free(cv);
        mt_printf(&b, "terms");
        for (uint32_t k = 0; k < n; k++)
            mt_printf(&b, " %llu:%u:%llu", (unsigned long long)mfc_term(s.cores[k]), mfc_role_code(mfc_role_of(s.cores[k])),
                      (unsigned long long)mfc_committed(s.cores[k])->version);
        CHECK_S(mt_str(&b), lines.v[li + 3]);
        mt_buf_free(&b);
        CHECK_I(s.failed, 0);
        sim_free(&s);
        mt_strs_free(&h);
        seeds++;
    }
    CHECK_I(seeds, 80);
    mt_strs_free(&lines);
    free(text);
    return mesh_check_done();
}
