#include "oc/json.h"
#include "testkit.h"

static void expect_report_roundtrip(void) {
    oc_arena a;
    oc_arena_init(&a);

    /* Writer: fixed field order, escaping, nesting, commas. */
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "name");
    oc_jsonw_str(&w, "quote\" back\\slash\nnewline\ttab");
    oc_jsonw_key(&w, "ctrl");
    oc_jsonw_strn(&w, "\x01", 1);
    oc_jsonw_key(&w, "utf8");
    oc_jsonw_str(&w, "caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87");
    oc_jsonw_key(&w, "n");
    oc_jsonw_null(&w);
    oc_jsonw_key(&w, "flag");
    oc_jsonw_bool(&w, 1);
    oc_jsonw_key(&w, "arr");
    oc_jsonw_arr_begin(&w);
    oc_jsonw_int(&w, -3);
    oc_jsonw_uint(&w, 9007199254740992ULL);
    oc_jsonw_float(&w, 0.5);
    oc_jsonw_arr_end(&w);
    oc_jsonw_key(&w, "empty");
    oc_jsonw_obj_begin(&w);
    oc_jsonw_obj_end(&w);
    oc_jsonw_obj_end(&w);
    CHECK(w.buf.data != NULL);
    CHECK_STR_CONTAINS(w.buf.data, "\"name\":\"quote\\\" back\\\\slash\\nnewline\\ttab\"");
    CHECK_STR_CONTAINS(w.buf.data, "\"ctrl\":\"\\u0001\"");
    CHECK_STR_CONTAINS(w.buf.data, "\"utf8\":\"caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87\"");

    /* Roundtrip through the parser. */
    oc_status st;
    oc_status_clear(&st);
    oc_json *v = oc_json_parse(&a, w.buf.data, w.buf.len, &st);
    CHECK(v != NULL);
    CHECK(v->type == OC_JSON_OBJ);
    const oc_json *name = oc_json_get(v, "name");
    CHECK_STR_EQ(oc_json_as_str(name, "?"), "quote\" back\\slash\nnewline\ttab");
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(v, "utf8"), "?"),
                 "caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87");
    CHECK(oc_json_as_bool(oc_json_get(v, "flag"), 0) == 1);
    const oc_json *arr = oc_json_get(v, "arr");
    CHECK(oc_json_size(arr) == 3);
    CHECK(arr->items[0]->type == OC_JSON_NUM && arr->items[0]->number == -3);
    CHECK(arr->items[1]->number == 9007199254740992.0);
    CHECK(arr->items[2]->number == 0.5);
    CHECK(oc_json_get(v, "missing") == NULL);
    oc_jsonw_destroy(&w);

    /* \uXXXX escapes including surrogate pairs. */
    const char *esc = "{\"s\":\"\\u0041\\uD83D\\uDE00\"}";
    oc_json *e = oc_json_parse(&a, esc, strlen(esc), &st);
    CHECK(e != NULL);
    const char *s = oc_json_as_str(oc_json_get(e, "s"), "?");
    CHECK_STR_EQ(s, "A\xf0\x9f\x98\x80");

    oc_arena_destroy(&a);
}

static void expect_parse_errors(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    const char *bad[] = {
        "{\"a\":1,}",      "{\"a\" 1}",   "{",     "[1, 2", "\"unterminated",
        "{\"a\":\"\\x\"}", "{} trailing", "[] []", "tru",   "{\"a\":}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        oc_status_clear(&st);
        oc_json *v = oc_json_parse(&a, bad[i], strlen(bad[i]), &st);
        oc__checks++;
        if (v != NULL || st.code != OC_ERR_PARSE) {
            oc__fails++;
            fprintf(stderr, "  FAIL: input '%s' should be rejected\n", bad[i]);
        }
    }

    /* NUL-terminated bound: len excludes trailing garbage after len. */
    oc_status_clear(&st);
    oc_json *v = oc_json_parse(&a, "{\"a\":1}JUNK", 7, &st);
    CHECK(v != NULL);
    oc_arena_destroy(&a);
}

int main(void) {
    expect_report_roundtrip();
    expect_parse_errors();
    TEST_SUMMARY();
}
