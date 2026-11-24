#include "oc/str.h"

#include <stdio.h>
#include <stdlib.h>

void oc_strbuf_init(oc_strbuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void oc_strbuf_destroy(oc_strbuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

int oc_strbuf_reserve(oc_strbuf *b, size_t extra) {
    size_t need = b->len + extra + 1;
    if (need <= b->cap)
        return 1;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need)
        cap *= 2;
    char *d = (char *)realloc(b->data, cap);
    if (!d)
        return 0;
    b->data = d;
    b->cap = cap;
    return 1;
}

int oc_strbuf_append(oc_strbuf *b, const char *s, size_t n) {
    if (!oc_strbuf_reserve(b, n))
        return 0;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

int oc_strbuf_appendc(oc_strbuf *b, char c) {
    return oc_strbuf_append(b, &c, 1);
}

int oc_strbuf_appends(oc_strbuf *b, const char *s) {
    return oc_strbuf_append(b, s, strlen(s));
}

int oc_strbuf_appendf(oc_strbuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0)
        return 0;
    if (!oc_strbuf_reserve(b, (size_t)needed))
        return 0;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)needed;
    return 1;
}
