#include "oc/graph.h"
#include "testkit.h"

int main(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    oc_node_id f1 = oc_graph_add_file(&g, "src/a.js");
    CHECK(f1 != OC_NODE_INVALID);
    CHECK(oc_graph_add_file(&g, "src/a.js") == f1); /* deduped */
    CHECK(g.n_nodes == 1);

    oc_node_id s1 = oc_graph_add_symbol(&g, OC_NODE_FUNCTION, "bigFn", "src/a.js", 10, 10, 90);
    oc_node_id s2 = oc_graph_add_symbol(&g, OC_NODE_FUNCTION, "smallFn", "src/a.js", 95, 95, 96);
    CHECK(s1 != OC_NODE_INVALID && s2 != OC_NODE_INVALID);
    CHECK(g.n_nodes == 3);
    CHECK(g.n_edges == 2); /* two CONTAINS edges auto-added */
    CHECK(g.edges[0].kind == OC_EDGE_CONTAINS);
    CHECK(g.edges[0].from == f1 && g.edges[0].to == s1);

    oc_node_id f2 = oc_graph_add_file(&g, "src/b.js");
    CHECK(oc_graph_find_file(&g, "src/a.js") == f1);
    CHECK(oc_graph_find_file(&g, "src/missing.js") == OC_NODE_INVALID);
    CHECK(oc_graph_add_edge(&g, s1, f2, OC_EDGE_IMPORTS));
    CHECK(g.n_edges == 3 && g.edges[2].kind == OC_EDGE_IMPORTS);

    /* Docs. */
    oc_doc *doc = oc_graph_doc(&g, f1);
    CHECK(doc != NULL);
    CHECK(oc_graph_doc(&g, f1) == doc);  /* stable pointer */
    CHECK(oc_graph_doc(&g, s1) == NULL); /* only files carry docs */
    CHECK(oc_doc_add_comment(doc, &a, 3, 1, "// TODO fix", 12));
    CHECK(doc->n_comments == 1);
    CHECK_STR_EQ(doc->comments[0].text, "// TODO fix");
    CHECK(doc->comments[0].line == 3);
    CHECK_STR_EQ(doc->path, "src/a.js");

    /* JSON dump is parseable and complete. */
    oc_jsonw w;
    oc_jsonw_init(&w);
    CHECK(oc_graph_write_json(&g, NULL, &w));
    oc_status st;
    oc_status_clear(&st);
    oc_json *dump = oc_json_parse(&a, w.buf.data, w.buf.len, &st);
    CHECK(dump != NULL);
    CHECK(oc_json_size(oc_json_get(dump, "nodes")) == 4);
    CHECK(oc_json_size(oc_json_get(dump, "edges")) == 3);
    const oc_json *n0 = oc_json_get(dump, "nodes");
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(n0->items[1], "kind"), "?"), "function");
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(n0->items[1], "name"), "?"), "bigFn");
    oc_jsonw_destroy(&w);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
    TEST_SUMMARY();
}
