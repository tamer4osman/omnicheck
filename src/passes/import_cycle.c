/* OC003 import-cycle -- graph pass over OC_EDGE_IMPORTS edges (plan
 * WBS 2.x, register-not-train: enabled per pack via enabled_passes).
 *
 * One finding per strongly connected component with more than one file,
 * computed by an iterative Tarjan SCC pass. Determinism: file nodes are
 * visited in graph order (discover.c sorts paths) and each cycle's file
 * list is emitted sorted, so reports are byte-stable across runs and
 * compilers. Self-imports (a file importing itself) are ignored; edges
 * are only ever resolved intra-repo (see imports.c), so an npm package
 * cycle can never fabricate a finding here.
 *
 * Although the pass interface is per-file, this is a whole-graph pass:
 * it computes once, from the first file node, and returns immediately
 * for every other file.
 */
#include "passes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cycle_state {
    oc_arena *arena;
    size_t n;
    /* CSR adjacency over file nodes that participate in imports edges. */
    oc_node_id *adj_head; /* per node id: first index into adj_items, SIZE_MAX = none */
    size_t *adj_items;    /* packed target node ids */
    size_t *adj_tail;     /* per node id: one-past-last index */
} cycle_state;

static const size_t OC_NO_IDX = (size_t)-1;

static int build_adjacency(const oc_pass_ctx *ctx, cycle_state *cs, oc_status *st) {
    const oc_graph *g = ctx->graph;
    cs->n = g->n_nodes;
    size_t n_edges = 0;
    for (size_t i = 0; i < g->n_edges; i++)
        if (g->edges[i].kind == OC_EDGE_IMPORTS)
            n_edges++;

    cs->adj_head = NULL;
    cs->adj_items = NULL;
    cs->adj_tail = NULL;
    if (n_edges == 0)
        return 1; /* no imports at all: nothing to compute */

    cs->adj_head = (oc_node_id *)oc_arena_alloc(ctx->arena, g->n_nodes * sizeof(oc_node_id));
    cs->adj_tail = (size_t *)oc_arena_alloc(ctx->arena, g->n_nodes * sizeof(size_t));
    cs->adj_items = (size_t *)oc_arena_alloc(ctx->arena, n_edges * sizeof(size_t));
    size_t *degree = (size_t *)oc_arena_alloc(ctx->arena, g->n_nodes * sizeof(size_t));
    if (!cs->adj_head || !cs->adj_tail || !cs->adj_items || !degree) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building cycle adjacency");
        return 0;
    }
    memset(degree, 0, g->n_nodes * sizeof(size_t));
    /* Count out-degrees over usable edges (file -> file, no self-import). */
    for (size_t i = 0; i < g->n_edges; i++) {
        const oc_edge *e = &g->edges[i];
        if (e->kind != OC_EDGE_IMPORTS)
            continue;
        if (g->nodes[e->from].kind != OC_NODE_FILE || g->nodes[e->to].kind != OC_NODE_FILE)
            continue;
        if (e->from == e->to)
            continue;
        degree[e->from]++;
    }
    /* CSR heads from prefix sums; tails start as copy of heads. */
    size_t running = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        cs->adj_head[i] = (oc_node_id)running;
        cs->adj_tail[i] = running;
        running += degree[i];
    }
    /* Fill targets via the tail cursor. */
    for (size_t i = 0; i < g->n_edges; i++) {
        const oc_edge *e = &g->edges[i];
        if (e->kind != OC_EDGE_IMPORTS)
            continue;
        if (g->nodes[e->from].kind != OC_NODE_FILE || g->nodes[e->to].kind != OC_NODE_FILE)
            continue;
        if (e->from == e->to)
            continue;
        cs->adj_items[cs->adj_tail[e->from]++] = e->to;
    }
    return 1;
}

/* Emits one finding for one SCC (size > 1), with a sorted file list.
 * SCCs larger than the collection buffer are reported with a truncated,
 * explicitly-marked chain rather than being silently dropped. */
static oc_code emit_cycle(const oc_pass_ctx *ctx, oc_node_id *scc, size_t n_scc, int truncated,
                          oc_status *st) {
    /* Sort member node ids by file path (deterministic report order). */
    for (size_t i = 1; i < n_scc; i++) {
        oc_node_id key = scc[i];
        size_t j = i;
        while (j > 0 &&
               strcmp(ctx->graph->nodes[scc[j - 1]].name, ctx->graph->nodes[key].name) > 0) {
            scc[j] = scc[j - 1];
            j--;
        }
        scc[j] = key;
    }

    size_t buf_cap = 64;
    for (size_t i = 0; i < n_scc; i++)
        buf_cap += strlen(ctx->graph->nodes[scc[i]].name) + 3;
    char *chain = (char *)oc_arena_alloc(ctx->arena, buf_cap);
    if (!chain) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting cycle message");
        return OC_ERR_ALLOC;
    }
    size_t off = 0;
    for (size_t i = 0; i < n_scc; i++) {
        int w = snprintf(chain + off, buf_cap - off, "%s%s", i ? " -> " : "",
                         ctx->graph->nodes[scc[i]].name);
        if (w < 0 || (size_t)w >= buf_cap - off)
            break;
        off += (size_t)w;
    }
    if (truncated && off + 8 < buf_cap)
        snprintf(chain + off, buf_cap - off, " -> ...");

    const oc_node *anchor = &ctx->graph->nodes[scc[0]];
    char *msg = oc_arena_sprintf(ctx->arena,
                                 "Import cycle among %s%zu files "
                                 "(sorted): %s",
                                 truncated ? "more than " : "", n_scc, chain);
    if (!msg) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
        return OC_ERR_ALLOC;
    }
    oc_finding_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.rule_id = "OC003";
    spec.rule_name = "import-cycle";
    spec.severity = OC_SEV_WARN;
    spec.file = anchor->name;
    spec.line = 1;
    spec.col = 1;
    spec.end_line = 1;
    spec.symbol = NULL; /* file-level finding */
    spec.message = msg;
    spec.fp_text = chain; /* distinct cycles sharing an anchor stay distinct */
    if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
        return OC_ERR_ALLOC;
    }
    return OC_OK;
}

static oc_code find_cycles(const oc_pass_ctx *ctx, cycle_state *cs, oc_status *st) {
    const oc_graph *g = ctx->graph;
    if (cs->adj_items == NULL)
        return OC_OK; /* no usable imports edges anywhere */
    size_t *index = (size_t *)oc_arena_alloc(ctx->arena, g->n_nodes * sizeof(size_t));
    size_t *lowlink = (size_t *)oc_arena_alloc(ctx->arena, g->n_nodes * sizeof(size_t));
    unsigned char *on_stack = (unsigned char *)oc_arena_alloc(ctx->arena, g->n_nodes);
    oc_node_id *tarjan_stack =
        (oc_node_id *)oc_arena_alloc(ctx->arena, g->n_nodes * sizeof(oc_node_id));
    if (!index || !lowlink || !on_stack || !tarjan_stack) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory running cycle detection");
        return OC_ERR_ALLOC;
    }
    for (size_t i = 0; i < g->n_nodes; i++)
        index[i] = OC_NO_IDX;
    for (size_t i = 0; i < g->n_nodes; i++)
        on_stack[i] = 0;

    size_t counter = 0, stack_n = 0;
    oc_code rc = OC_OK;

    /* Iterative Tarjan. Frame stack: (node, next-child-cursor). */
    typedef struct {
        oc_node_id v;
        size_t child;
    } tj_frame;
    size_t frame_cap = 64;
    tj_frame *frames = (tj_frame *)oc_arena_alloc(ctx->arena, frame_cap * sizeof(tj_frame));
    if (!frames) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory running cycle detection");
        return OC_ERR_ALLOC;
    }
    size_t n_frames = 0;

    for (size_t start = 0; start < g->n_nodes; start++) {
        if (g->nodes[start].kind != OC_NODE_FILE || index[start] != OC_NO_IDX)
            continue;
        if (cs->adj_head[start] == cs->adj_tail[start])
            continue; /* no out-edges under CSR: cannot open a cycle */

        index[start] = lowlink[start] = counter++;
        tarjan_stack[stack_n++] = (oc_node_id)start;
        on_stack[start] = 1;
        frames[0].v = (oc_node_id)start;
        frames[0].child = cs->adj_head[start];
        n_frames = 1;

        while (n_frames > 0) {
            tj_frame *fr = &frames[n_frames - 1];
            oc_node_id v = fr->v;
            if (fr->child < cs->adj_tail[v]) {
                oc_node_id w = (oc_node_id)cs->adj_items[fr->child++];
                if (index[w] == OC_NO_IDX) {
                    index[w] = lowlink[w] = counter++;
                    tarjan_stack[stack_n++] = w;
                    on_stack[w] = 1;
                    if (n_frames == frame_cap) {
                        frame_cap *= 2;
                        tj_frame *grown =
                            (tj_frame *)oc_arena_alloc(ctx->arena, frame_cap * sizeof(tj_frame));
                        if (!grown) {
                            oc_status_set(st, OC_ERR_ALLOC,
                                          "out of memory running cycle detection");
                            return OC_ERR_ALLOC;
                        }
                        memcpy(grown, frames, n_frames * sizeof(tj_frame));
                        frames = grown;
                    }
                    frames[n_frames].v = w;
                    frames[n_frames].child = cs->adj_head[w];
                    n_frames++;
                } else if (on_stack[w]) {
                    if (index[w] < lowlink[v])
                        lowlink[v] = index[w];
                }
            } else {
                n_frames--;
                if (lowlink[v] == index[v]) {
                    /* Root of an SCC: pop members. */
                    oc_node_id scc_buf[512];
                    size_t n_scc = 0;
                    int overflow = 0;
                    while (stack_n > 0) {
                        oc_node_id w = tarjan_stack[--stack_n];
                        on_stack[w] = 0;
                        if (n_scc < sizeof(scc_buf) / sizeof(scc_buf[0]))
                            scc_buf[n_scc++] = w;
                        else
                            overflow = 1;
                        if (w == v)
                            break;
                    }
                    if (rc == OC_OK && n_scc > 1)
                        rc = emit_cycle(ctx, scc_buf, n_scc, overflow, st);
                    if (rc != OC_OK)
                        return rc;
                }
                if (n_frames > 0) {
                    tj_frame *parent = &frames[n_frames - 1];
                    if (lowlink[v] < lowlink[parent->v])
                        lowlink[parent->v] = lowlink[v];
                }
            }
        }
    }
    oc_status_clear(st);
    return OC_OK;
}

static oc_code run_import_cycle(const oc_pass_ctx *ctx, oc_status *st) {
    const oc_graph *g = ctx->graph;
    /* Whole-graph pass: compute exactly once, from the first file node. */
    oc_node_id first_file = OC_NODE_INVALID;
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FILE) {
            first_file = (oc_node_id)i;
            break;
        }
    }
    if (ctx->file_id != first_file)
        return OC_OK;

    cycle_state cs;
    memset(&cs, 0, sizeof(cs));
    cs.arena = ctx->arena;
    if (!build_adjacency(ctx, &cs, st))
        return st->code != OC_OK ? (oc_code)st->code : OC_ERR_INTERNAL;
    oc_code rc = find_cycles(ctx, &cs, st);
    oc_status_clear(st);
    return rc;
}

const oc_pass oc_pass_import_cycle = {
    "OC003",
    "import-cycle",
    OC_SEV_WARN,
    "Strongly connected component of imports with more than one file (OC003, "
    "computed on the first file of the run)",
    run_import_cycle,
};
