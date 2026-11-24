/*
 * Arena allocator: the memory backbone of OmniCheck (plan risk R2
 * mitigation). All long-lived parse products (graph nodes, docs, pack
 * data, finding strings) are allocated from arenas and released in one
 * call, which makes leaks and double-frees structurally impossible for
 * the analysis pipeline.
 */
#ifndef OC_ARENA_H
#define OC_ARENA_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_arena_block {
    struct oc_arena_block *next;
    size_t used;
    size_t cap;
    unsigned char bytes[];
} oc_arena_block;

typedef struct oc_arena {
    oc_arena_block *head;
    oc_arena_block *tail;
    size_t block_size;      /* default capacity of a new block */
    size_t total_allocated; /* useful bytes handed out (diagnostics) */
} oc_arena;

void oc_arena_init(oc_arena *a);
void oc_arena_set_block_size(oc_arena *a, size_t bytes);
void *oc_arena_alloc(oc_arena *a, size_t n);
char *oc_arena_strdup(oc_arena *a, const char *s);
char *oc_arena_strndup(oc_arena *a, const char *s, size_t n);
/* Formatted string allocated in the arena; NULL on OOM. */
char *oc_arena_sprintf(oc_arena *a, const char *fmt, ...);
void oc_arena_destroy(oc_arena *a);

#ifdef __cplusplus
}
#endif
#endif /* OC_ARENA_H */
