/* jobs.rs's job_word, parse_job, hex and unhex. */
#include "mesh/jobs/id.h"
#include "mesh/rs_parse.h"

#include <stdio.h>

void mj_job_word(mj_job_id id, char out[MJ_JOB_WORD_MAX]) {
    snprintf(out, MJ_JOB_WORD_MAX, "%llu.%llu", (unsigned long long)(id >> 32), (unsigned long long)(id & 0xffffffffu));
}

bool mj_parse_job(const char *s, mj_job_id *out) {
    const char *dot = strchr(s, '.');
    if (!dot) return false;
    uint64_t t;
    uint32_t n;
    if (!rs_u64(s, (size_t)(dot - s), &t) || !rs_u32(dot + 1, strlen(dot + 1), &n)) return false;
    *out = t << 32 | n;
    return true;
}

void mj_hex(const mj_hash *h, char out[65]) {
    static const char d[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = d[h->b[i] >> 4];
        out[2 * i + 1] = d[h->b[i] & 15];
    }
    out[64] = 0;
}

bool mj_unhex(const char *s, mj_hash *out) {
    if (strlen(s) != 64) return false;
    mj_hash h;
    for (int i = 0; i < 32; i++) {
        uint64_t v;
        if (!rs_parse_uint(s + 2 * i, 2, 16, 255, &v)) return false;
        h.b[i] = (uint8_t)v;
    }
    *out = h;
    return true;
}
