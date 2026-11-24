/*
 * String view and growable string buffer.
 *
 * Ownership rule: oc_strbuf owns its heap buffer (init/append/destroy).
 * Arena-allocated strings must never be freed individually. A strbuf is
 * reserved for transient, growable data (reports, protocol payloads);
 * everything that outlives a function stays in the arena.
 */
#ifndef OC_STR_H
#define OC_STR_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_strview {
    const char *p;
    size_t n;
} oc_strview;

static inline oc_strview oc_sv(const char *s) {
    oc_strview v;
    v.p = s;
    v.n = s ? strlen(s) : 0;
    return v;
}

static inline int oc_sv_eq(oc_strview a, oc_strview b) {
    return a.n == b.n && (a.n == 0 || memcmp(a.p, b.p, a.n) == 0);
}

static inline int oc_sv_empty(oc_strview v) {
    return v.n == 0;
}

/* FNV-1a 64-bit over a view. */
static inline uint64_t oc_sv_hash(oc_strview v) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < v.n; i++) {
        h ^= (unsigned char)v.p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

typedef struct oc_strbuf {
    char *data;
    size_t len;
    size_t cap;
} oc_strbuf;

void oc_strbuf_init(oc_strbuf *b);
/* All append functions return 1 on success, 0 on OOM. */
int oc_strbuf_reserve(oc_strbuf *b, size_t extra);
int oc_strbuf_append(oc_strbuf *b, const char *s, size_t n);
int oc_strbuf_appendc(oc_strbuf *b, char c);
int oc_strbuf_appends(oc_strbuf *b, const char *s);
int oc_strbuf_appendf(oc_strbuf *b, const char *fmt, ...);
void oc_strbuf_destroy(oc_strbuf *b);

#ifdef __cplusplus
}
#endif
#endif /* OC_STR_H */
