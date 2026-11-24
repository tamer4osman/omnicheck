#include "oc/graph.h"

#include <stdlib.h>

#include "oc/coverage.h"

const char *oc_node_kind_str(oc_node_kind k) {
    switch (k) {
    case OC_NODE_FILE:
        return "file";
    case OC_NODE_FUNCTION:
        return "function";
    case OC_NODE_CLASS:
        return "class";
    case OC_NODE_VARIABLE:
        return "variable";
    }
    return "unknown";
}

const char *oc_edge_kind_str(oc_edge_kind k) {
    switch (k) {
    case OC_EDGE_CONTAINS:
        return "contains";
    case OC_EDGE_IMPORTS:
        return "imports";
    }
    return "unknown";
}

void oc_graph_init(oc_graph *g, oc_arena *arena) {
    g->arena = arena;
    g->nodes = NULL;
    g->n_nodes = 0;
    g->cap_nodes = 0;
    g->edges = NULL;
    g->n_edges = 0;
    g->cap_edges = 0;
    g->docs = NULL;
    g->docs_cap = 0;
}

void oc_graph_destroy(oc_graph *g) {
    free(g->nodes);
    free(g->edges);
    g->nodes = NULL;
    g->edges = NULL;
    g->n_nodes = 0;
    g->cap_nodes = 0;
    g->n_edges = 0;
    g->cap_edges = 0;
}

static int graph_reserve_nodes(oc_graph *g, size_t need) {
    if (g->n_nodes + need <= g->cap_nodes)
        return 1;
    size_t cap = g->cap_nodes ? g->cap_nodes : 64;
    while (cap < g->n_nodes + need)
        cap *= 2;
    oc_node *nodes = (oc_node *)realloc(g->nodes, cap * sizeof(oc_node));
    if (!nodes)
        return 0;
    g->nodes = nodes;
    g->cap_nodes = cap;
    return 1;
}

static int graph_reserve_edges(oc_graph *g, size_t need) {
    if (g->n_edges + need <= g->cap_edges)
        return 1;
    size_t cap = g->cap_edges ? g->cap_edges : 128;
    while (cap < g->n_edges + need)
        cap *= 2;
    oc_edge *edges = (oc_edge *)realloc(g->edges, cap * sizeof(oc_edge));
    if (!edges)
        return 0;
    g->edges = edges;
    g->cap_edges = cap;
    return 1;
}

static void graph_push_node(oc_graph *g, const oc_node *n, oc_node_id *out) {
    if (!graph_reserve_nodes(g, 1)) {
        *out = OC_NODE_INVALID;
        return;
    }
    g->nodes[g->n_nodes] = *n;
    *out = (oc_node_id)g->n_nodes;
    g->n_nodes++;
}

oc_node_id oc_graph_add_file(oc_graph *g, const char *path) {
    oc_node_id existing = oc_graph_find_file(g, path);
    if (existing != OC_NODE_INVALID)
        return existing;
    oc_node n;
    memset(&n, 0, sizeof(n));
    n.kind = OC_NODE_FILE;
    n.name = oc_arena_strdup(g->arena, path);
    n.path = n.name;
    oc_node_id id;
    graph_push_node(g, &n, &id);
    return id;
}

oc_node_id oc_graph_add_symbol(oc_graph *g, oc_node_kind kind, const char *name,
                               const char *file_path, size_t decl_line, size_t def_start,
                               size_t def_end) {
    oc_node n;
    memset(&n, 0, sizeof(n));
    n.kind = kind;
    n.name = oc_arena_strdup(g->arena, name);
    n.path = oc_arena_strdup(g->arena, file_path);
    n.decl_line = decl_line;
    n.def_start_line = def_start;
    n.def_end_line = def_end;
    oc_node_id id;
    graph_push_node(g, &n, &id);
    if (id == OC_NODE_INVALID)
        return OC_NODE_INVALID;
    oc_node_id fid = oc_graph_find_file(g, file_path);
    if (fid == OC_NODE_INVALID) {
        fid = oc_graph_add_file(g, file_path);
    }
    (void)oc_graph_add_edge(g, fid, id, OC_EDGE_CONTAINS);
    return id;
}

/* SPRINT-17 (B3): deep-copy the signature into the graph arena. The
 * node itself is heap-owned but every string it references must be
 * arena-owned (the graph outlives the caller's storage). */
int oc_graph_set_params(oc_graph *g, oc_node_id id, const oc_param *params, size_t n) {
    if (id == OC_NODE_INVALID || id >= g->n_nodes)
        return 0;
    oc_node *node = &g->nodes[id];
    node->params = NULL;
    node->n_params = 0;
    if (n == 0)
        return 1;
    oc_param *copy = (oc_param *)oc_arena_alloc(g->arena, n * sizeof(oc_param));
    if (!copy)
        return 0;
    for (size_t i = 0; i < n; i++) {
        copy[i].name = params[i].name ? oc_arena_strdup(g->arena, params[i].name) : NULL;
        copy[i].type = params[i].type ? oc_arena_strdup(g->arena, params[i].type) : NULL;
        copy[i].has_default = params[i].has_default;
        if (params[i].name && !copy[i].name)
            return 0; /* arena exhausted mid-copy: caller reports OOM */
    }
    node->params = copy;
    node->n_params = n;
    return 1;
}

int oc_graph_add_edge(oc_graph *g, oc_node_id from, oc_node_id to, oc_edge_kind kind) {
    if (from == OC_NODE_INVALID || to == OC_NODE_INVALID)
        return 0;
    if (!graph_reserve_edges(g, 1))
        return 0;
    g->edges[g->n_edges].from = from;
    g->edges[g->n_edges].to = to;
    g->edges[g->n_edges].kind = kind;
    g->n_edges++;
    return 1;
}

oc_node_id oc_graph_find_file(const oc_graph *g, const char *path) {
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FILE && strcmp(g->nodes[i].name, path) == 0)
            return (oc_node_id)i;
    }
    return OC_NODE_INVALID;
}

oc_doc *oc_graph_doc(oc_graph *g, oc_node_id file_id) {
    if (file_id == OC_NODE_INVALID || file_id >= g->n_nodes)
        return NULL;
    if (g->nodes[file_id].kind != OC_NODE_FILE)
        return NULL;
    /* Docs are stored as arena-allocated blocks keyed by file node id.
     * Sprint 0 keeps a simple 1:1 side table sized to the node count. */
    if (g->docs == NULL) {
        size_t cap = g->n_nodes > 16 ? g->n_nodes : 16;
        g->docs = (oc_doc **)oc_arena_alloc(g->arena, cap * sizeof(oc_doc *));
        g->docs_cap = cap;
        if (g->docs)
            memset(g->docs, 0, cap * sizeof(oc_doc *));
    }
    if ((size_t)file_id >= g->docs_cap) {
        size_t cap = g->docs_cap;
        while (cap <= (size_t)file_id)
            cap *= 2;
        oc_doc **nd = (oc_doc **)oc_arena_alloc(g->arena, cap * sizeof(oc_doc *));
        if (!nd)
            return NULL;
        memcpy(nd, g->docs, g->docs_cap * sizeof(oc_doc *));
        memset(nd + g->docs_cap, 0, (cap - g->docs_cap) * sizeof(oc_doc *));
        g->docs = nd;
        g->docs_cap = cap;
    }
    if (g->docs[file_id] == NULL) {
        oc_doc *d = (oc_doc *)oc_arena_alloc(g->arena, sizeof(oc_doc));
        if (!d)
            return NULL;
        d->path = g->nodes[file_id].name;
        d->comments = NULL;
        d->n_comments = 0;
        d->cap_comments = 0;
        g->docs[file_id] = d;
    }
    return g->docs[file_id];
}

int oc_doc_add_comment(oc_doc *doc, oc_arena *a, size_t line, size_t col, const char *text,
                       size_t len) {
    if (doc->n_comments == doc->cap_comments) {
        size_t cap = doc->cap_comments ? doc->cap_comments * 2 : 8;
        oc_comment *nc = (oc_comment *)oc_arena_alloc(a, cap * sizeof(oc_comment));
        if (!nc)
            return 0;
        if (doc->n_comments)
            memcpy(nc, doc->comments, doc->n_comments * sizeof(oc_comment));
        doc->comments = nc;
        doc->cap_comments = cap;
    }
    oc_comment *c = &doc->comments[doc->n_comments++];
    c->line = line;
    c->col = col;
    c->len = len;
    c->text = oc_arena_strndup(a, text, len);
    return c->text != NULL;
}

/* Coverage fields appear only when a coverage table was composed into the
 * analysis (oc check --coverage / oc graph --coverage); their absence is
 * how consumers tell "no coverage data" from "0% covered". */
static void node_write_coverage(const oc_node *n, const oc_coverage *cov, oc_jsonw *w) {
    if (!cov || n->kind != OC_NODE_FUNCTION)
        return;
    size_t anchors = 0, covered = 0;
    if (!oc_coverage_lookup(cov, n->path, n->def_start_line, n->def_end_line, &anchors, &covered))
        return;
    oc_jsonw_key(w, "coverage_anchors");
    oc_jsonw_uint(w, anchors);
    oc_jsonw_key(w, "coverage_covered");
    oc_jsonw_uint(w, covered);
    if (anchors > 0) {
        size_t pct = covered * 100 / anchors;
        oc_jsonw_key(w, "coverage_pct");
        oc_jsonw_uint(w, pct);
        size_t comp = n->complexity;
        size_t crap = comp * comp * (100 - pct) / 100 + comp;
        oc_jsonw_key(w, "crap");
        oc_jsonw_uint(w, crap);
    }
}

int oc_graph_write_json(const oc_graph *g, const oc_coverage *cov, oc_jsonw *w) {
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "nodes");
    oc_jsonw_arr_begin(w);
    for (size_t i = 0; i < g->n_nodes; i++) {
        const oc_node *n = &g->nodes[i];
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "id");
        oc_jsonw_uint(w, i);
        oc_jsonw_key(w, "kind");
        oc_jsonw_str(w, oc_node_kind_str(n->kind));
        oc_jsonw_key(w, "name");
        oc_jsonw_str(w, n->name);
        oc_jsonw_key(w, "path");
        oc_jsonw_str(w, n->path);
        if (n->kind != OC_NODE_FILE) {
            oc_jsonw_key(w, "decl_line");
            oc_jsonw_uint(w, n->decl_line);
            oc_jsonw_key(w, "def_start");
            oc_jsonw_uint(w, n->def_start_line);
            oc_jsonw_key(w, "def_end");
            oc_jsonw_uint(w, n->def_end_line);
            if (n->complexity > 0) {
                oc_jsonw_key(w, "complexity");
                oc_jsonw_uint(w, n->complexity);
            }
            node_write_coverage(n, cov, w);
        }
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_arr_end(w);
    oc_jsonw_key(w, "edges");
    oc_jsonw_arr_begin(w);
    for (size_t i = 0; i < g->n_edges; i++) {
        const oc_edge *e = &g->edges[i];
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "from");
        oc_jsonw_uint(w, e->from);
        oc_jsonw_key(w, "to");
        oc_jsonw_uint(w, e->to);
        oc_jsonw_key(w, "kind");
        oc_jsonw_str(w, oc_edge_kind_str(e->kind));
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_arr_end(w);
    oc_jsonw_obj_end(w);
    return 1;
}
