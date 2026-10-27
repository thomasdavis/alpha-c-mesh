/* Job words and hash hex, as jobs.rs writes and reads them. */
#include "mesh/jobs/id.h"
#include "mesh/check.h"

int main(void) {
    char w[MJ_JOB_WORD_MAX];
    mj_job_word((uint64_t)3 << 32 | 7, w);
    CHECK_S(w, "3.7");
    mj_job_word(UINT64_MAX, w);
    CHECK_S(w, "4294967295.4294967295");
    mj_job_id id;
    CHECK(mj_parse_job("3.7", &id) && id == ((uint64_t)3 << 32 | 7));
    CHECK(mj_parse_job("+1.+2", &id) && id == ((uint64_t)1 << 32 | 2));
    CHECK(!mj_parse_job("3", &id) && !mj_parse_job("3.", &id) && !mj_parse_job(".3", &id) && !mj_parse_job("3.4294967296", &id));
    CHECK(!mj_parse_job("1.2.3", &id)); /* "2.3" is not a u32 */
    mj_hash h;
    for (int i = 0; i < 32; i++) h.b[i] = (uint8_t)(i * 37 + 1);
    char x[65];
    mj_hex(&h, x);
    CHECK_S(x, "01264b7095badf04294e7398bde2072c51769bc0e50a2f54799ec3e80d32577c");
    mj_hash back;
    CHECK(mj_unhex(x, &back) && mj_hash_eq(&back, &h));
    CHECK(mj_unhex("01264B7095BADF04294E7398BDE2072C51769BC0E50A2F54799EC3E80D32577C", &back) && mj_hash_eq(&back, &h));
    CHECK(!mj_unhex("0126", &back) && !mj_unhex("zz264b7095badf04294e7398bde2072c5176a0c5ea0f34597ea3c8ed12375c81", &back));
    uint64_t words[4];
    mj_hash_words(&h, words);
    CHECK_U(words[0], 0x01264b7095badf04u);
    mj_hash again = mj_hash_of_words(words);
    CHECK(mj_hash_eq(&again, &h));
    return mesh_check_done();
}
