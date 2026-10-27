/* native.rs's usage on Linux: fields after the parenthesised command: utime 14, stime 15, rss 24. */
#include "mesh/proc_usage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool mesh_proc_usage(pid_t pid, uint64_t *cpu_ns, uint64_t *rss) {
    char path[64], buf[4096];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *close_paren = strrchr(buf, ')');
    if (!close_paren || close_paren[1] == 0) return false;
    /* rest starts at the state field (field 3). */
    char *rest = close_paren + 2, *save = NULL;
    char *tok = strtok_r(rest, " ", &save);
    unsigned long long ut = 0, stt = 0, pages = 0;
    for (int i = 0; tok && i <= 21; i++, tok = strtok_r(NULL, " ", &save)) {
        if (i == 11) ut = strtoull(tok, NULL, 10);
        if (i == 12) stt = strtoull(tok, NULL, 10);
        if (i == 21) {
            pages = strtoull(tok, NULL, 10);
            long tick = sysconf(_SC_CLK_TCK), page = sysconf(_SC_PAGESIZE);
            if (tick < 1) tick = 1;
            if (page < 1) page = 1;
            *cpu_ns = (ut + stt) * 1000000000ull / (unsigned long long)tick;
            *rss = pages * (unsigned long long)page;
            return true;
        }
    }
    return false;
}
