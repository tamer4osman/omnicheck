#include "oc/finding.h"
#include "testkit.h"

static void push(oc_finding_vec *v, oc_arena *a, const char *rule, const char *name,
                 oc_severity sev, const char *file, size_t line, const char *symbol,
                 const char *msg, const char *fp_text) {
    oc_finding_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.rule_id = rule;
    spec.rule_name = name;
    spec.severity = sev;
    spec.file = file;
    spec.line = line;
    spec.col = 1;
    spec.end_line = line + 10;
    spec.symbol = symbol;
    spec.message = msg;
    spec.fp_text = fp_text;
    CHECK(oc_findings_push(v, a, &spec));
}

int main(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_finding_vec v;
    oc_findings_init(&v);

    push(&v, &a, "OC002", "todo-comment", OC_SEV_INFO, "src/b.js", 9, NULL, "TODO marker: x",
         "todo x");
    push(&v, &a, "OC001", "long-function", OC_SEV_WARN, "src/a.js", 30, "bigFn",
         "Function 'bigFn' spans 80 lines", NULL);
    push(&v, &a, "OC001", "long-function", OC_SEV_ERROR, "src/a.js", 10, "oldFn",
         "Function 'oldFn' spans 120 lines", NULL);

    /* Fingerprints: stable across identical push, distinct otherwise. */
    oc_finding_vec v2;
    oc_findings_init(&v2);
    push(&v2, &a, "OC002", "todo-comment", OC_SEV_INFO, "src/b.js", 9, NULL, "TODO marker: x",
         "todo x");
    CHECK_STR_EQ(v.items[0].fingerprint, v2.items[0].fingerprint);
    CHECK_STR_CONTAINS(v.items[1].message, "bigFn");
    CHECK(v.items[1].severity == OC_SEV_WARN);
    CHECK(v.items[1].end_line == 40);

    /* Sort: (file, line, col, rule) deterministic order. */
    oc_findings_sort(&v);
    CHECK_STR_EQ(v.items[0].file, "src/a.js");
    CHECK(v.items[0].line == 10);
    CHECK_STR_EQ(v.items[1].file, "src/a.js");
    CHECK(v.items[1].line == 30);
    CHECK_STR_EQ(v.items[2].file, "src/b.js");

    CHECK(oc_findings_max_severity(&v) == OC_SEV_ERROR);
    CHECK(v.items[0].triaged == 0);
    CHECK(v.items[0].triage_rank == -1);

    oc_findings_destroy(&v);
    oc_findings_destroy(&v2);
    oc_arena_destroy(&a);
    TEST_SUMMARY();
}
