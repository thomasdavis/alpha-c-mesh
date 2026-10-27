/* @ajax/mesh-member-list: lists of fleet members (system ids 0..MAX_SYSTEMS) and their masks, in the
 * text form every fleet message uses: "0,2,3", or "-" for none; a placement that is "every member
 * that can" (Rust's `Option<Vec<u32>>` None) prints "all" (sched::at_word) or "*" (the document). */
#ifndef MESH_MEMBER_LIST_H
#define MESH_MEMBER_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t *v;
    size_t n, cap;
} ml_list;

/* Rust's Option<Vec<u32>>: some false is None. */
typedef struct {
    bool some;
    ml_list l;
} ml_opt;

void ml_push(ml_list *l, uint32_t k);
bool ml_has(const ml_list *l, uint32_t k);
long ml_find(const ml_list *l, uint32_t k); /* index, or -1 */
void ml_remove_value(ml_list *l, uint32_t k); /* every occurrence (Rust's retain(|x| x != k)) */
void ml_remove_at(ml_list *l, size_t i);
void ml_sort(ml_list *l);
bool ml_eq(const ml_list *a, const ml_list *b);
ml_list ml_clone(const ml_list *l);
ml_list ml_of(const uint32_t *v, size_t n);
void ml_free(ml_list *l);

ml_opt ml_none(void);
ml_opt ml_some(const uint32_t *v, size_t n);
bool ml_opt_eq(const ml_opt *a, const ml_opt *b);
ml_opt ml_opt_clone(const ml_opt *o);
void ml_opt_free(ml_opt *o);
/* Rust's `a.as_ref().is_none_or(|a| a.contains(&k))`: None counts as every member. */
bool ml_opt_has_or_all(const ml_opt *o, uint32_t k);

/* "-" or "0,2,3" (fleet::list, sched::list); a new string. */
char *ml_text(const ml_list *l);
/* sched::at_word: "all" for None, else ml_text. */
char *ml_at_word(const ml_opt *o);
/* fleet::parse_list: "-" is empty; each a u32 < MAX_SYSTEMS. On error *err is the Rust's message. */
bool ml_parse(const char *s, ml_list *out, char **err);

uint32_t ml_mask(const ml_list *l);  /* fleet::mask_of */
ml_list ml_members(uint32_t mask);   /* fleet::members_of: bits 0..32 set */
int ml_popcount(uint32_t x);

#endif
