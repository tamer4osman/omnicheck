/*
 * JSON writer and parser. One dependency-free implementation serves all
 * three JSON surfaces of the tool: deterministic reports, declarative
 * language packs, and the OmniDecide sidecar protocol.
 */
#ifndef OC_JSON_H
#define OC_JSON_H

#include "arena.h"
#include "str.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_JSON_MAX_DEPTH 32

/* ---------------------------------------------------------------- writer */

typedef struct oc_jsonw {
    oc_strbuf buf;
    int depth;
    unsigned char has_item[OC_JSON_MAX_DEPTH];
    unsigned char in_key[OC_JSON_MAX_DEPTH]; /* a key was just emitted */
} oc_jsonw;

void oc_jsonw_init(oc_jsonw *w);
void oc_jsonw_destroy(oc_jsonw *w); /* frees the buffer; use w->buf.data first */

void oc_jsonw_obj_begin(oc_jsonw *w);
void oc_jsonw_obj_end(oc_jsonw *w);
void oc_jsonw_arr_begin(oc_jsonw *w);
void oc_jsonw_arr_end(oc_jsonw *w);

void oc_jsonw_key(oc_jsonw *w, const char *key);
void oc_jsonw_str(oc_jsonw *w, const char *s); /* NULL emits null */
void oc_jsonw_strn(oc_jsonw *w, const char *s, size_t n);
void oc_jsonw_int(oc_jsonw *w, long long v);
void oc_jsonw_uint(oc_jsonw *w, size_t v);
void oc_jsonw_float(oc_jsonw *w, double v); /* %.6g */
void oc_jsonw_bool(oc_jsonw *w, int v);
void oc_jsonw_null(oc_jsonw *w);

/* ---------------------------------------------------------------- parser */

typedef enum oc_json_type {
    OC_JSON_NULL = 0,
    OC_JSON_BOOL,
    OC_JSON_NUM,
    OC_JSON_STR,
    OC_JSON_ARR,
    OC_JSON_OBJ
} oc_json_type;

typedef struct oc_json {
    oc_json_type type;
    int boolean;
    double number;
    const char *str; /* NUL-terminated, arena-owned */
    struct oc_json **items;
    size_t n_items;
    size_t cap_items;  /* growth capacity; the arena never reclaims, so
                        * geometric doubling keeps total cost O(n) */
    const char **keys; /* OBJ fields, insertion order preserved */
    struct oc_json **vals;
    size_t n_fields;
    size_t cap_keys;
    size_t cap_vals;
} oc_json;

/* Parse text (NUL-terminated; len is the bounded byte length) into an
 * arena-allocated DOM. Returns NULL and sets st on any error. */
oc_json *oc_json_parse(oc_arena *a, const char *text, size_t len, oc_status *st);

/* JSONC variant for tsconfig.json/jsconfig.json: // and slash-star
 * comments plus trailing commas are tolerated (string payloads are
 * never touched); everything else stays strict JSON. */
oc_json *oc_json_parse_lenient(oc_arena *a, const char *text, size_t len, oc_status *st);

const oc_json *oc_json_get(const oc_json *obj, const char *key);
size_t oc_json_size(const oc_json *arr);
const char *oc_json_as_str(const oc_json *v, const char *fallback);
double oc_json_as_num(const oc_json *v, double fallback);
int oc_json_as_bool(const oc_json *v, int fallback);

#ifdef __cplusplus
}
#endif
#endif /* OC_JSON_H */
