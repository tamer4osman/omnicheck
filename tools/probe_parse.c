/* Debug probe: parse a file and dump the comment doc. */
#include "oc/arena.h"
#include "oc/graph.h"
#include "oc/parser.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <file> <grammar>\n", argv[0]);
        return 2;
    }
    oc_arena a;
    oc_arena_init(&a);
    char *data = NULL;
    size_t len = 0;
    oc_status st;
    oc_status_clear(&st);
    if (oc_read_file(argv[1], &data, &len, &st) != OC_OK) {
        fprintf(stderr, "read error: %s\n", st.msg);
        return 1;
    }
    oc_parser *p = oc_parser_new(&a, &st);
    printf("backend=%s\n", oc_parser_backend_name(p));
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_parser_input in;
    memset(&in, 0, sizeof(in));
    in.path = argv[1];
    in.source = data;
    in.len = len;
    in.grammar = argv[2];
    oc_parse_out out;
    memset(&out, 0, sizeof(out));
    oc_code rc = oc_parser_parse_file(p, &in, &g, &out, &st);
    if (rc != OC_OK) {
        fprintf(stderr, "parse error: %s\n", st.msg);
        return 1;
    }
    printf("parse_ok=%d error_nodes=%zu first_line=%zu first_col=%zu\n",
           out.diag.had_error ? 0 : 1, out.diag.error_nodes, out.diag.first_line,
           out.diag.first_col);
    printf("nodes=%zu edges=%zu\n", g.n_nodes, g.n_edges);
    for (size_t i = 0; i < g.n_nodes; i++) {
        printf("  node %zu kind=%s name=%s lines=%zu..%zu\n", i,
               oc_node_kind_str(g.nodes[i].kind), g.nodes[i].name,
               g.nodes[i].def_start_line, g.nodes[i].def_end_line);
    }
    oc_doc *doc = oc_graph_doc(&g, 0);
    printf("comments=%zu\n", doc ? doc->n_comments : 0);
    if (doc) {
        for (size_t i = 0; i < doc->n_comments; i++)
            printf("  comment %zu line=%zu col=%zu text=\"%.60s\"\n", i,
                   doc->comments[i].line, doc->comments[i].col,
                   doc->comments[i].text);
    }
    return 0;
}
