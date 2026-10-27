/* The rules, one awk program's worth: strip the comment, the leading blanks
 * and braces, and a predicate guard; take the opcode up to the first blank or
 * ';'; split it at '.'; then the rules by its first token. */
#include "mesh/ptx_float_rules.h"

#include <stdbool.h>
#include <string.h>

#define MAX_TOKENS 32

static bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_ident(char c) { return is_lower(c) || is_digit(c) || c == '_'; }

static bool tok_is(const char *t, size_t n, const char *w) { return strlen(w) == n && memcmp(t, w, n) == 0; }
static bool tok_in(const char *t, size_t n, const char *const *ws) {
    for (; *ws; ws++)
        if (tok_is(t, n, *ws)) return true;
    return false;
}

static const char *const FLOATS[] = {"f16", "f16x2", "bf16", "bf16x2", "tf32", "e4m3", "e5m2", "e4m3x2", "e5m2x2", "f32", "f64", NULL};
static const char *const NARROW[] = {"f16", "f16x2", "bf16", "bf16x2", "tf32", "e4m3", "e5m2", "e4m3x2", "e5m2x2", NULL};
static const char *const ROUNDED[] = {"add", "sub", "mul", "div", "rcp", "sqrt", NULL};
static const char *const FLUSHED[] = {"abs", "neg", "min", "max", "setp", "set", "slct", NULL};
static const char *const PLAIN[] = {"selp", "copysign", "testp", "mov", "ld", "ldu", "st", NULL};

const char *mesh_ptx_float_line(const char *line, size_t len) {
    /* sub(/\/\/.*\/, "", line) */
    for (size_t i = 0; i + 1 < len; i++)
        if (line[i] == '/' && line[i + 1] == '/') { len = i; break; }
    /* gsub(/^[ \t{]+/, "", line) */
    size_t a = 0;
    while (a < len && (line[a] == ' ' || line[a] == '\t' || line[a] == '{')) a++;
    const char *s = line + a;
    len -= a;
    /* A predicate guard: @p, @!p, @%p1 ... followed by blanks. */
    if (len >= 2 && s[0] == '@') {
        size_t i = 1;
        if (i < len && s[i] == '!') i++;
        if (i < len && s[i] == '%') {
            i++;
            size_t id = i;
            while (i < len && (is_lower(s[i]) || is_digit(s[i]) || s[i] == '_')) i++;
            if (i > id && i < len && (s[i] == ' ' || s[i] == '\t')) {
                while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
                s += i;
                len -= i;
            }
        }
    }
    /* line !~ /^[a-z][a-z0-9_]*\./ { next } */
    if (len == 0 || !is_lower(s[0])) return NULL;
    size_t i = 1;
    while (i < len && is_ident(s[i])) i++;
    if (i >= len || s[i] != '.') return NULL;
    /* op = line up to the first blank or ';' */
    size_t oplen = 0;
    while (oplen < len && s[oplen] != ' ' && s[oplen] != '\t' && s[oplen] != ';') oplen++;
    const char *tok[MAX_TOKENS];
    size_t tl[MAX_TOKENS];
    int n = 0;
    for (size_t p = 0, start = 0; p <= oplen; p++) {
        if (p == oplen || s[p] == '.') {
            if (n < MAX_TOKENS) { tok[n] = s + start; tl[n] = p - start; n++; }
            start = p + 1;
        }
    }
    bool fl = false, f32 = false, narrow = false, approx = false, full = false, ftz = false, rn = false;
    for (int k = 1; k < n; k++) {
        if (tok_in(tok[k], tl[k], FLOATS)) fl = true;
        if (tok_is(tok[k], tl[k], "f32")) f32 = true;
    }
    if (!fl) return NULL;
    /* m = "." op ".": the tokens, the first one included. */
    for (int k = 0; k < n; k++) {
        if (tok_in(tok[k], tl[k], NARROW)) narrow = true;
        if (tok_is(tok[k], tl[k], "approx")) approx = true;
        if (tok_is(tok[k], tl[k], "full")) full = true;
        if (tok_is(tok[k], tl[k], "ftz")) ftz = true;
        if (tok_is(tok[k], tl[k], "rn")) rn = true;
    }
    const char *t1 = tok[0];
    size_t l1 = tl[0];
    if (narrow) return "16-bit / 8-bit float";
    if (approx) return ".approx";
    if (full) return "div.full";
    if (tok_is(t1, l1, "fma") || tok_is(t1, l1, "mad")) return "fused multiply-add";
    if (tok_in(t1, l1, ROUNDED)) {
        if (!rn) return "no .rn (ptxas may fuse it, per arch)";
        if (f32 && !ftz) return "f32 without .ftz";
        return NULL;
    }
    if (tok_in(t1, l1, FLUSHED)) return f32 && !ftz ? "f32 without .ftz" : NULL;
    if (tok_is(t1, l1, "cvt")) {
        bool src32 = tok_is(tok[n - 1], tl[n - 1], "f32");
        bool narrowing = n >= 2 && tok_is(tok[n - 2], tl[n - 2], "f32") && tok_is(tok[n - 1], tl[n - 1], "f64");
        return (src32 || narrowing) && !ftz ? "f32 convert without .ftz" : NULL;
    }
    if (!tok_in(t1, l1, PLAIN)) return "float instruction with no rule (approximate or not covered)";
    return NULL;
}

int mesh_ptx_float_check(const char *path, const char *text, size_t len, FILE *out) {
    int bad = 0;
    size_t lineno = 0;
    for (size_t at = 0; at < len && bad < 5;) {
        size_t end = at;
        while (end < len && text[end] != '\n') end++;
        lineno++;
        const char *why = mesh_ptx_float_line(text + at, end - at);
        if (why) {
            bad++;
            if (out) fprintf(out, "%s:%zu: %s: %.*s\n", path, lineno, why, (int)(end - at), text + at);
        }
        at = end + 1;
    }
    return bad;
}
