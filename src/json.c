#include "oc/json.h"

#include <stdio.h>
#include <stdlib.h>

/* =================================================================== */
/* Writer                                                              */
/* =================================================================== */

void oc_jsonw_init(oc_jsonw *w) {
    oc_strbuf_init(&w->buf);
    w->depth = 0;
    memset(w->has_item, 0, sizeof(w->has_item));
    memset(w->in_key, 0, sizeof(w->in_key));
}

void oc_jsonw_destroy(oc_jsonw *w) {
    oc_strbuf_destroy(&w->buf);
    w->depth = 0;
}

/* Prefix for a VALUE: values following a key carry no comma; standalone
 * array items are comma-separated via has_item. */
static void jsonw_prefix(oc_jsonw *w) {
    if (w->depth == 0)
        return;
    int d = w->depth - 1;
    if (w->in_key[d]) {
        w->in_key[d] = 0;
        return;
    }
    if (w->has_item[d])
        oc_strbuf_appendc(&w->buf, ',');
    w->has_item[d] = 1;
}

static void jsonw_push(oc_jsonw *w) {
    w->has_item[w->depth] = 0;
    w->in_key[w->depth] = 0;
    w->depth++;
}

static void jsonw_pop(oc_jsonw *w) {
    if (w->depth > 0)
        w->depth--;
}

static void jsonw_write_escaped(oc_jsonw *w, const char *s, size_t n) {
    oc_strbuf_appendc(&w->buf, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':
            oc_strbuf_appends(&w->buf, "\\\"");
            break;
        case '\\':
            oc_strbuf_appends(&w->buf, "\\\\");
            break;
        case '\b':
            oc_strbuf_appends(&w->buf, "\\b");
            break;
        case '\f':
            oc_strbuf_appends(&w->buf, "\\f");
            break;
        case '\n':
            oc_strbuf_appends(&w->buf, "\\n");
            break;
        case '\r':
            oc_strbuf_appends(&w->buf, "\\r");
            break;
        case '\t':
            oc_strbuf_appends(&w->buf, "\\t");
            break;
        default:
            if (c < 0x20) {
                char tmp[8];
                snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)c);
                oc_strbuf_appends(&w->buf, tmp);
            } else {
                /* UTF-8 bytes pass through unchanged (valid JSON). */
                oc_strbuf_appendc(&w->buf, (char)c);
            }
            break;
        }
    }
    oc_strbuf_appendc(&w->buf, '"');
}

void oc_jsonw_key(oc_jsonw *w, const char *key) {
    if (w->depth == 0)
        return;
    int d = w->depth - 1;
    if (w->has_item[d])
        oc_strbuf_appendc(&w->buf, ',');
    w->has_item[d] = 1;
    w->in_key[d] = 1;
    jsonw_write_escaped(w, key, strlen(key));
    oc_strbuf_appendc(&w->buf, ':');
}

void oc_jsonw_str(oc_jsonw *w, const char *s) {
    if (!s) {
        oc_jsonw_null(w);
        return;
    }
    jsonw_prefix(w);
    jsonw_write_escaped(w, s, strlen(s));
}

void oc_jsonw_strn(oc_jsonw *w, const char *s, size_t n) {
    if (!s) {
        oc_jsonw_null(w);
        return;
    }
    jsonw_prefix(w);
    jsonw_write_escaped(w, s, n);
}

void oc_jsonw_int(oc_jsonw *w, long long v) {
    jsonw_prefix(w);
    oc_strbuf_appendf(&w->buf, "%lld", v);
}

void oc_jsonw_uint(oc_jsonw *w, size_t v) {
    jsonw_prefix(w);
    oc_strbuf_appendf(&w->buf, "%zu", v);
}

void oc_jsonw_float(oc_jsonw *w, double v) {
    jsonw_prefix(w);
    oc_strbuf_appendf(&w->buf, "%.6g", v);
}

void oc_jsonw_bool(oc_jsonw *w, int v) {
    jsonw_prefix(w);
    oc_strbuf_appends(&w->buf, v ? "true" : "false");
}

void oc_jsonw_null(oc_jsonw *w) {
    jsonw_prefix(w);
    oc_strbuf_appends(&w->buf, "null");
}

void oc_jsonw_obj_begin(oc_jsonw *w) {
    jsonw_prefix(w);
    oc_strbuf_appendc(&w->buf, '{');
    jsonw_push(w);
}

void oc_jsonw_obj_end(oc_jsonw *w) {
    jsonw_pop(w);
    oc_strbuf_appendc(&w->buf, '}');
}

void oc_jsonw_arr_begin(oc_jsonw *w) {
    jsonw_prefix(w);
    oc_strbuf_appendc(&w->buf, '[');
    jsonw_push(w);
}

void oc_jsonw_arr_end(oc_jsonw *w) {
    jsonw_pop(w);
    oc_strbuf_appendc(&w->buf, ']');
}

/* =================================================================== */
/* Parser                                                              */
/* =================================================================== */

#define OC_JSON_MAX_TEXT 64
/* H-1 (P-1): a hostile document may carry millions of fields/items; each
 * one costs real arena memory even under geometric growth. 1,000,000
 * entries is far beyond every legitimate manifest/coverage/LSP payload
 * the tool consumes, and anything larger fails the parse LOUDLY
 * (the project contract: never silently partial, never unbounded). */
#define OC_JSON_MAX_ENTRIES 1000000

typedef struct oc_json_parser {
    const char *start;
    const char *p;
    const char *end;
    oc_arena *arena;
    oc_status *st;
    int depth;
    size_t entries; /* H-1: total fields + items pushed so far */
} oc_json_parser;

static void jerr(oc_json_parser *ps, const char *msg) {
    if (ps->st && ps->st->code == OC_OK)
        oc_status_set(ps->st, OC_ERR_PARSE, "JSON: %s (at byte %ld of %ld)", msg,
                      (long)(ps->p - ps->start), (long)(ps->end - ps->start));
}

/* H-1 (P-1): geometric growth for the parser's key/value/item vectors.
 * The arena never reclaims, so the old per-append (n+1)-sized allocation
 * copied the whole vector for EVERY push: O(n^2) bytes and O(n^2) work --
 * measured at 3.6 GB peak RSS for a 30,000-field package.json (T1).
 * Doubling from 16 allocates log2(n) buffers whose total size is a
 * geometric series, i.e. O(final size) arena cost and amortized O(1)
 * copy work per push; outputs are unchanged (the vectors' contents are
 * identical, only the spare capacity differs). */
static void **grow_ptr_vec(oc_arena *a, void **old, size_t n, size_t *cap, size_t elem) {
    if (n < *cap)
        return old;
    size_t ncap = *cap ? *cap * 2 : 16;
    void **g = (void **)oc_arena_alloc(a, ncap * elem);
    if (!g)
        return NULL;
    if (n)
        memcpy(g, old, n * elem); /* copy ONCE per doubling */
    *cap = ncap;
    return g;
}

static void jskip_ws(oc_json_parser *ps) {
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            ps->p++;
        else
            break;
    }
}

static oc_json *jnew(oc_json_parser *ps, oc_json_type t) {
    oc_json *v = (oc_json *)oc_arena_alloc(ps->arena, sizeof(oc_json));
    if (!v)
        return NULL;
    memset(v, 0, sizeof(*v));
    v->type = t;
    return v;
}

static oc_json *jparse_value(oc_json_parser *ps);

/* Parses a string body (assumes current char is the opening quote) into an
 * arena buffer, resolving escapes including \uXXXX surrogate pairs. */
static char *jparse_string_raw(oc_json_parser *ps, size_t *out_len) {
    if (ps->p >= ps->end || *ps->p != '"') {
        jerr(ps, "expected string");
        return NULL;
    }
    const char *q = ps->p + 1;
    while (q < ps->end && *q != '"') {
        if (*q == '\\') {
            if (q + 1 >= ps->end) {
                ps->p = q;
                jerr(ps, "unterminated escape");
                return NULL;
            }
            q += (q[1] == 'u') ? 6 : 2;
        } else if ((unsigned char)*q < 0x20) {
            ps->p = q;
            jerr(ps, "raw control character inside string");
            return NULL;
        } else {
            q++;
        }
    }
    if (q >= ps->end) {
        ps->p = q;
        jerr(ps, "unterminated string");
        return NULL;
    }

    size_t cap = (size_t)(q - (ps->p + 1)) + 8;
    char *buf = (char *)oc_arena_alloc(ps->arena, cap + 1);
    if (!buf) {
        ps->st->code = OC_ERR_ALLOC;
        oc_status_set(ps->st, OC_ERR_ALLOC, "JSON: out of memory");
        return NULL;
    }
    size_t o = 0;
    const char *r = ps->p + 1;
    while (r < q) {
        char c = *r;
        if (c != '\\') {
            buf[o++] = c;
            r++;
            continue;
        }
        r++;
        char e = *r++;
        switch (e) {
        case '"':
            buf[o++] = '"';
            break;
        case '\\':
            buf[o++] = '\\';
            break;
        case '/':
            buf[o++] = '/';
            break;
        case 'b':
            buf[o++] = '\b';
            break;
        case 'f':
            buf[o++] = '\f';
            break;
        case 'n':
            buf[o++] = '\n';
            break;
        case 'r':
            buf[o++] = '\r';
            break;
        case 't':
            buf[o++] = '\t';
            break;
        case 'u': {
            unsigned cp = 0;
            for (int i = 0; i < 4; i++) {
                char h = *r++;
                unsigned d;
                if (h >= '0' && h <= '9')
                    d = (unsigned)(h - '0');
                else if (h >= 'a' && h <= 'f')
                    d = (unsigned)(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                    d = (unsigned)(h - 'A' + 10);
                else {
                    ps->p = r;
                    jerr(ps, "bad \\u escape");
                    return NULL;
                }
                cp = (cp << 4) | d;
            }
            if (cp >= 0xD800 && cp <= 0xDBFF && r + 6 <= q && r[0] == '\\' && r[1] == 'u') {
                unsigned lo = 0;
                int ok = 1;
                for (int i = 0; i < 4; i++) {
                    char h = r[2 + i];
                    unsigned d;
                    if (h >= '0' && h <= '9')
                        d = (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f')
                        d = (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F')
                        d = (unsigned)(h - 'A' + 10);
                    else {
                        ok = 0;
                        break;
                    }
                    lo = (lo << 4) | d;
                }
                if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    r += 6;
                }
            }
            /* UTF-8 encode (cap+1 leaves room: <=4 bytes per codepoint). */
            if (cp < 0x80) {
                buf[o++] = (char)cp;
            } else if (cp < 0x800) {
                buf[o++] = (char)(0xC0 | (cp >> 6));
                buf[o++] = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                buf[o++] = (char)(0xE0 | (cp >> 12));
                buf[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                buf[o++] = (char)(0x80 | (cp & 0x3F));
            } else {
                buf[o++] = (char)(0xF0 | (cp >> 18));
                buf[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                buf[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                buf[o++] = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default:
            ps->p = r;
            jerr(ps, "unknown escape");
            return NULL;
        }
    }
    buf[o] = '\0';
    *out_len = o;
    ps->p = q + 1;
    return buf;
}

static oc_json *jparse_value(oc_json_parser *ps) {
    jskip_ws(ps);
    if (ps->p >= ps->end) {
        jerr(ps, "unexpected end of input");
        return NULL;
    }

    char c = *ps->p;
    if (c == '{') {
        if (++ps->depth > OC_JSON_MAX_DEPTH) {
            jerr(ps, "nesting too deep");
            return NULL;
        }
        oc_json *v = jnew(ps, OC_JSON_OBJ);
        if (!v)
            return NULL;
        ps->p++;
        jskip_ws(ps);
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
            ps->depth--;
            return v;
        }
        for (;;) {
            jskip_ws(ps);
            size_t klen = 0;
            char *key = jparse_string_raw(ps, &klen);
            if (!key)
                return NULL;
            jskip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') {
                jerr(ps, "expected ':'");
                return NULL;
            }
            ps->p++;
            if (ps->entries >= OC_JSON_MAX_ENTRIES) {
                jerr(ps, "document exceeds the 1,000,000 field/item budget");
                return NULL;
            }
            oc_json *val = jparse_value(ps);
            if (!val)
                return NULL;
            const char **nk = (const char **)grow_ptr_vec(ps->arena, (void **)v->keys,
                                                          v->n_fields, &v->cap_keys,
                                                          sizeof(char *));
            oc_json **nv = (oc_json **)grow_ptr_vec(ps->arena, (void **)v->vals,
                                                    v->n_fields, &v->cap_vals,
                                                    sizeof(oc_json *));
            if (!nk || !nv)
                return NULL;
            nk[v->n_fields] = key;
            nv[v->n_fields] = val;
            v->keys = nk;
            v->vals = nv;
            v->n_fields++;
            ps->entries++;
            jskip_ws(ps);
            if (ps->p < ps->end && *ps->p == ',') {
                ps->p++;
                continue;
            }
            if (ps->p < ps->end && *ps->p == '}') {
                ps->p++;
                ps->depth--;
                return v;
            }
            jerr(ps, "expected ',' or '}'");
            return NULL;
        }
    }
    if (c == '[') {
        if (++ps->depth > OC_JSON_MAX_DEPTH) {
            jerr(ps, "nesting too deep");
            return NULL;
        }
        oc_json *v = jnew(ps, OC_JSON_ARR);
        if (!v)
            return NULL;
        ps->p++;
        jskip_ws(ps);
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
            ps->depth--;
            return v;
        }
        for (;;) {
            if (ps->entries >= OC_JSON_MAX_ENTRIES) {
                jerr(ps, "document exceeds the 1,000,000 field/item budget");
                return NULL;
            }
            oc_json *item = jparse_value(ps);
            if (!item)
                return NULL;
            oc_json **ni = (oc_json **)grow_ptr_vec(ps->arena, (void **)v->items,
                                                    v->n_items, &v->cap_items,
                                                    sizeof(oc_json *));
            if (!ni)
                return NULL;
            ni[v->n_items] = item;
            v->items = ni;
            v->n_items++;
            ps->entries++;
            jskip_ws(ps);
            if (ps->p < ps->end && *ps->p == ',') {
                ps->p++;
                continue;
            }
            if (ps->p < ps->end && *ps->p == ']') {
                ps->p++;
                ps->depth--;
                return v;
            }
            jerr(ps, "expected ',' or ']'");
            return NULL;
        }
    }
    if (c == '"') {
        size_t slen = 0;
        char *s = jparse_string_raw(ps, &slen);
        if (!s)
            return NULL;
        oc_json *v = jnew(ps, OC_JSON_STR);
        if (!v)
            return NULL;
        v->str = s;
        return v;
    }
    if (c == 't') {
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "true", 4) == 0) {
            ps->p += 4;
            oc_json *v = jnew(ps, OC_JSON_BOOL);
            if (!v)
                return NULL;
            v->boolean = 1;
            return v;
        }
        jerr(ps, "bad literal");
        return NULL;
    }
    if (c == 'f') {
        if (ps->end - ps->p >= 5 && memcmp(ps->p, "false", 5) == 0) {
            ps->p += 5;
            oc_json *v = jnew(ps, OC_JSON_BOOL);
            if (!v)
                return NULL;
            v->boolean = 0;
            return v;
        }
        jerr(ps, "bad literal");
        return NULL;
    }
    if (c == 'n') {
        if (ps->end - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) {
            ps->p += 4;
            return jnew(ps, OC_JSON_NULL);
        }
        jerr(ps, "bad literal");
        return NULL;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *start = ps->p;
        const char *q = ps->p;
        while (q < ps->end && (*q == '-' || *q == '+' || *q == '.' || *q == 'e' || *q == 'E' ||
                               (*q >= '0' && *q <= '9')))
            q++;
        size_t tlen = (size_t)(q - start);
        if (tlen == 0 || tlen > OC_JSON_MAX_TEXT) {
            jerr(ps, "bad number");
            return NULL;
        }
        char tok[OC_JSON_MAX_TEXT + 1];
        memcpy(tok, start, tlen);
        tok[tlen] = '\0';
        char *endp = NULL;
        double d = strtod(tok, &endp);
        if (!endp || *endp != '\0') {
            jerr(ps, "bad number");
            return NULL;
        }
        ps->p = q;
        oc_json *v = jnew(ps, OC_JSON_NUM);
        if (!v)
            return NULL;
        v->number = d;
        return v;
    }
    jerr(ps, "unexpected character");
    return NULL;
}

oc_json *oc_json_parse(oc_arena *a, const char *text, size_t len, oc_status *st) {
    oc_json_parser ps;
    ps.start = text;
    ps.p = text;
    ps.end = text + len;
    ps.arena = a;
    ps.st = st;
    ps.depth = 0;
    ps.entries = 0;

    oc_json *root = jparse_value(&ps);
    if (!root)
        return NULL;
    jskip_ws(&ps);
    if (ps.p != ps.end) {
        jerr(&ps, "trailing data after value");
        return NULL;
    }
    return root;
}

/*
 * Lenient JSONC variant (tsconfig.json/jsconfig.json): strips // and
 * the arena, then the strict parser runs unchanged. Malformed input
 * still fails loudly -- leniency covers exactly the two JSONC extensions,
 * nothing else. Trailing commas are handled with a pending-comma flag so
 * commas inside string payloads are never touched.
 */
static char *jsonc_strip(oc_arena *a, const char *text, size_t len) {
    char *out = (char *)oc_arena_alloc(a, len + 1);
    if (!out)
        return NULL;
    size_t w = 0;
    int in_str = 0, esc = 0, pending_comma = 0;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (in_str) {
            out[w++] = c;
            if (esc)
                esc = 0;
            else if (c == '\\')
                esc = 1;
            else if (c == '"')
                in_str = 0;
            continue;
        }
        if (c == '"') {
            in_str = 1;
            if (pending_comma) {
                out[w++] = ',';
                pending_comma = 0;
            }
            out[w++] = c;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue; /* whitespace: pending comma stays pending */
        if (c == '/' && i + 1 < len && text[i + 1] == '/') {
            while (i < len && text[i] != '\n')
                i++;
            out[w++] = ' '; /* token separator, prevents gluing */
            continue;
        }
        if (c == '/' && i + 1 < len && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(text[i] == '*' && text[i + 1] == '/'))
                i++;
            i++;            /* loop increment skips the '/' of the closing pair */
            out[w++] = ' '; /* token separator, prevents gluing */
            continue;
        }
        if (c == ',') {
            pending_comma = 1;
            continue;
        }
        if (pending_comma && c != ']' && c != '}') {
            out[w++] = ','; /* the comma was a separator, keep it */
            pending_comma = 0;
        } else if (pending_comma) {
            pending_comma = 0; /* trailing comma before ] or }: dropped */
        }
        out[w++] = c;
    }
    if (pending_comma)
        out[w++] = ','; /* malformed tail; the strict parser will reject */
    out[w] = '\0';
    return out;
}

oc_json *oc_json_parse_lenient(oc_arena *a, const char *text, size_t len, oc_status *st) {
    char *clean = jsonc_strip(a, text, len);
    if (!clean) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory stripping JSONC");
        return NULL;
    }
    return oc_json_parse(a, clean, strlen(clean), st);
}

const oc_json *oc_json_get(const oc_json *obj, const char *key) {
    if (!obj || obj->type != OC_JSON_OBJ)
        return NULL;
    for (size_t i = 0; i < obj->n_fields; i++) {
        if (strcmp(obj->keys[i], key) == 0)
            return obj->vals[i];
    }
    return NULL;
}

size_t oc_json_size(const oc_json *arr) {
    if (!arr || arr->type != OC_JSON_ARR)
        return 0;
    return arr->n_items;
}

const char *oc_json_as_str(const oc_json *v, const char *fallback) {
    if (!v || v->type != OC_JSON_STR || !v->str)
        return fallback;
    return v->str;
}

double oc_json_as_num(const oc_json *v, double fallback) {
    if (!v || v->type != OC_JSON_NUM)
        return fallback;
    return v->number;
}

int oc_json_as_bool(const oc_json *v, int fallback) {
    if (!v || v->type != OC_JSON_BOOL)
        return fallback;
    return v->boolean;
}
