#include "oc/str.h"
#include "testkit.h"

int main(void) {
    oc_strbuf b;
    oc_strbuf_init(&b);
    CHECK(oc_strbuf_appends(&b, "hello"));
    CHECK(oc_strbuf_appends(&b, " "));
    CHECK(oc_strbuf_appendf(&b, "%s-%d", "world", 7));
    CHECK_STR_EQ(b.data, "hello world-7");
    oc_strbuf_destroy(&b);

    /* Growth across the initial capacity boundary. */
    oc_strbuf_init(&b);
    for (int i = 0; i < 1000; i++)
        oc_strbuf_appendf(&b, "%04d", i);
    CHECK(b.len == 4000);
    CHECK(b.cap >= b.len + 1);
    CHECK(memcmp(b.data, "000000010002", 12) == 0);
    CHECK(memcmp(b.data + 3996, "0999", 4) == 0);
    oc_strbuf_destroy(&b);

    oc_strview a = oc_sv("same");
    oc_strview c = oc_sv("same");
    oc_strview d = oc_sv("differ");
    CHECK(oc_sv_eq(a, c));
    CHECK(!oc_sv_eq(a, d));
    CHECK(oc_sv_hash(a) == oc_sv_hash(c));
    CHECK(oc_sv_hash(a) != oc_sv_hash(d));
    CHECK(!oc_sv_empty(a));
    TEST_SUMMARY();
}
