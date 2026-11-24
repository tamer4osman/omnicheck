/* Pass-level tests: build a graph + docs by hand (no parser involved),
 * run OC001 / OC002, verify findings, thresholds and fingerprint rules. */
#include "passes/passes.h"
#include "testkit.h"

static void setup(oc_arena *a, oc_graph *g, oc_pack *pack, const char *path, oc_node_id *file_id,
                  oc_doc **doc) {
    oc_graph_init(g, a);
    *file_id = oc_graph_add_file(g, path);
    oc_arena_strndup(a, "ts-js", 5);
    memset(pack, 0, sizeof(*pack));
    static char *exts[1];
    exts[0] = (char *)".js";
    pack->id = (char *)"ts-js";
    pack->grammar = (char *)"javascript";
    pack->extensions = exts;
    pack->n_extensions = 1;
    pack->n_passes = 0;
    *doc = oc_graph_doc(g, *file_id);
}

int main(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_config cfg;
    oc_config_init(&cfg);

    /* ---------------- OC001 long-function ---------------- */
    oc_graph g;
    oc_pack pack;
    oc_node_id fid;
    oc_doc *doc;
    setup(&a, &g, &pack, "src/big.js", &fid, &doc);
    oc_graph_add_symbol(&g, OC_NODE_FUNCTION, "bigFn", "src/big.js", 10, 10, 90);
    oc_graph_add_symbol(&g, OC_NODE_FUNCTION, "smallFn", "src/big.js", 95, 95, 96);
    oc_graph_add_symbol(&g, OC_NODE_FUNCTION, "otherFn", "src/other.js", 1, 1, 80);

    oc_finding_vec out;
    oc_findings_init(&out);
    oc_pass_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.graph = &g;
    ctx.file_id = fid;
    ctx.doc = doc;
    ctx.pack = &pack;
    ctx.config = &cfg;
    ctx.arena = &a;
    ctx.out = &out;

    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_pass_long_function.run(&ctx, &st) == OC_OK);
    CHECK(out.n == 1); /* bigFn only (default threshold 60) */
    CHECK_STR_EQ(out.items[0].rule_id, "OC001");
    CHECK_STR_EQ(out.items[0].rule_name, "long-function");
    CHECK_STR_EQ(out.items[0].symbol, "bigFn");
    CHECK(out.items[0].line == 10 && out.items[0].end_line == 90);
    CHECK_STR_CONTAINS(out.items[0].message, "spans 81 lines");

    char fp_first[OC_FINGERPRINT_HEX_LEN + 1];
    snprintf(fp_first, sizeof(fp_first), "%s", out.items[0].fingerprint);

    /* Threshold override silences it. */
    oc_findings_destroy(&out);
    oc_findings_init(&out);
    CHECK(oc_config_set(&cfg, "OC001.max_lines", "100"));
    CHECK(oc_pass_long_function.run(&ctx, &st) == OC_OK);
    CHECK(out.n == 0);

    /* ...and with a tight threshold, both big functions fire. */
    oc_config_destroy(&cfg);
    oc_config_init(&cfg);
    CHECK(oc_config_set(&cfg, "OC001.max_lines", "5"));
    oc_findings_destroy(&out);
    oc_findings_init(&out);
    CHECK(oc_pass_long_function.run(&ctx, &st) == OC_OK);
    CHECK(out.n == 1);
    oc_config_destroy(&cfg);
    oc_config_init(&cfg);

    /* Fingerprint is stable across re-runs (determinism contract). */
    oc_findings_destroy(&out);
    oc_findings_init(&out);
    CHECK(oc_pass_long_function.run(&ctx, &st) == OC_OK);
    CHECK_STR_EQ(out.items[0].fingerprint, fp_first);

    /* ---------------- OC002 todo-comment ---------------- */
    oc_findings_destroy(&out);
    oc_findings_init(&out);
    CHECK(oc_doc_add_comment(doc, &a, 2, 1, "// TODO: fix this later", 23));
    CHECK(oc_doc_add_comment(doc, &a, 5, 1, "// plain note", 13));
    CHECK(oc_doc_add_comment(doc, &a, 8, 1, "/* FIXME lowercase marker */", 28));
    CHECK(oc_doc_add_comment(doc, &a, 12, 1, "# HACK wording", 14));
    CHECK(oc_doc_add_comment(doc, &a, 15, 1, "// AUTOTODOMATIC not a marker", 29));
    ctx.doc = doc;
    CHECK(oc_pass_todo_comment.run(&ctx, &st) == OC_OK);
    CHECK(out.n == 3); /* TODO + FIXME + HACK; AUTOTODOMATIC is glued text */
    if (out.n == 3) {
        CHECK(out.items[0].severity == OC_SEV_INFO);
        CHECK_STR_CONTAINS(out.items[0].message, "TODO");
        CHECK_STR_CONTAINS(out.items[0].fingerprint, "");
        CHECK(strcmp(out.items[0].fingerprint, out.items[1].fingerprint) != 0);
        CHECK(strcmp(out.items[1].fingerprint, out.items[2].fingerprint) != 0);
    }

    /* Unknown pass ids in packs are ignored by the CLI (registry check). */
    size_t n_all = 0;
    const oc_pass *const *all = oc_pass_all(&n_all);
    CHECK(n_all == OC_PASS_COUNT); /* sprint 5: OC009-011 join the registry */
    CHECK(oc_pass_lookup("OC001") == &oc_pass_long_function);
    CHECK(oc_pass_lookup("OC002") == &oc_pass_todo_comment);
    CHECK(oc_pass_lookup("OC008") == &oc_pass_duplicate_code);
    CHECK(oc_pass_lookup("OC009") == &oc_pass_dep_vuln);
    CHECK(oc_pass_lookup("OC010") == &oc_pass_licence);
    CHECK(oc_pass_lookup("OC011") == &oc_pass_secret);
    CHECK(oc_pass_lookup("OC999") == NULL);
    CHECK(all != NULL);

    oc_findings_destroy(&out);
    oc_graph_destroy(&g);
    oc_config_destroy(&cfg);
    oc_arena_destroy(&a);
    TEST_SUMMARY();
}
