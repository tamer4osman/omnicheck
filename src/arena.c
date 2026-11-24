#include "oc/arena.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define OC_ARENA_DEFAULT_BLOCK (64u * 1024u)
#define OC_ARENA_ALIGN         16u

static oc_arena_block *arena_block_new(size_t cap) {
    oc_arena_block *b = (oc_arena_block *)malloc(sizeof(*b) + cap);
    if (!b)
        return NULL;
    b->next = NULL;
    b->used = 0;
    b->cap = cap;
    return b;
}

void oc_arena_init(oc_arena *a) {
    a->head = NULL;
    a->tail = NULL;
    a->block_size = OC_ARENA_DEFAULT_BLOCK;
    a->total_allocated = 0;
}

void oc_arena_set_block_size(oc_arena *a, size_t bytes) {
    a->block_size = bytes > 0 ? bytes : OC_ARENA_DEFAULT_BLOCK;
}

void *oc_arena_alloc(oc_arena *a, size_t n) {
    size_t need = (n + (OC_ARENA_ALIGN - 1)) & ~(size_t)(OC_ARENA_ALIGN - 1);
    if (need == 0)
        need = OC_ARENA_ALIGN;

    if (!a->tail || a->tail->cap - a->tail->used < need + OC_ARENA_ALIGN) {
        size_t cap = a->block_size;
        if (need + OC_ARENA_ALIGN > cap)
            cap = need + OC_ARENA_ALIGN;
        oc_arena_block *b = arena_block_new(cap);
        if (!b)
            return NULL;
        if (a->tail)
            a->tail->next = b;
        else
            a->head = b;
        a->tail = b;
    }

    /* Align the absolute pointer: the header before bytes[] is not a
     * multiple of the alignment on every ABI. */
    uintptr_t raw = (uintptr_t)a->tail->bytes + a->tail->used;
    uintptr_t aligned = (raw + (OC_ARENA_ALIGN - 1)) & ~(uintptr_t)(OC_ARENA_ALIGN - 1);
    void *p = (void *)aligned;
    a->tail->used = (size_t)(aligned - (uintptr_t)a->tail->bytes) + need;
    a->total_allocated += need;
    return p;
}

char *oc_arena_strndup(oc_arena *a, const char *s, size_t n) {
    char *d = (char *)oc_arena_alloc(a, n + 1);
    if (!d)
        return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

char *oc_arena_strdup(oc_arena *a, const char *s) {
    return oc_arena_strndup(a, s, strlen(s));
}

char *oc_arena_sprintf(oc_arena *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0)
        return NULL;
    char *d = (char *)oc_arena_alloc(a, (size_t)needed + 1);
    if (!d)
        return NULL;
    va_start(ap, fmt);
    vsnprintf(d, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    return d;
}

void oc_arena_destroy(oc_arena *a) {
    oc_arena_block *b = a->head;
    while (b) {
        oc_arena_block *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
    a->tail = NULL;
    a->total_allocated = 0;
}
