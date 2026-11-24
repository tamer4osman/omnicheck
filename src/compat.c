#include "oc/compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/discover.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/pack.h"
#include "oc/parser.h"
#include "oc/surface.h"

/* One analyzed side of the diff: the file set, the parsed graph (for
 * function nodes) and the export records. All arena-owned. Paths are
 * compared RELATIVE to the side's analysis root, so v1/ and v2/ trees
 * diff by their contents, not by the root directory name. */
typedef struct compat_side {
    const char *root;
    size_t root_len;
    oc_graph graph;
    oc_path_vec files;
    oc_bind_vec binds;
    oc_export_vec exports;
    oc_hint_vec hints;
    /* SPRINT-17 (B3): grammar id per analyzed file (from the pack
     * match), driving the per-language signature policy. Heap arrays,
     * path strings point into s->files, grammar ids into the pack set
     * (both alive for the whole run). */
    const char **gram_paths;
    const char **gram_ids;
    size_t n_grams, cap_grams;
} compat_side;

static void side_init(compat_side *s) {
    memset(s, 0, sizeof(*s));
    /* graph is initialized in side_build, where the arena is known */
    oc_paths_init(&s->files);
    oc_surface_binds_init(&s->binds);
    oc_surface_exports_init(&s->exports);
    oc_surface_hints_init(&s->hints);
}

static void side_destroy(compat_side *s) {
    oc_surface_exports_destroy(&s->exports);
    oc_surface_binds_destroy(&s->binds);
    oc_surface_hints_destroy(&s->hints);
    oc_paths_destroy(&s->files);
    oc_graph_destroy(&s->graph);
    free(s->gram_paths);
    free(s->gram_ids);
    s->gram_paths = NULL;
    s->gram_ids = NULL;
    s->n_grams = s->cap_grams = 0;
}

static int side_record_grammar(compat_side *s, const char *path, const char *grammar) {
    if (s->n_grams == s->cap_grams) {
        size_t cap = s->cap_grams ? s->cap_grams * 2 : 8;
        const char **np = (const char **)realloc(s->gram_paths, cap * sizeof(*np));
        const char **ni = (const char **)realloc(s->gram_ids, cap * sizeof(*ni));
        if (!np || !ni) {
            free(np);
            free(ni);
            return 0;
        }
        s->gram_paths = np;
        s->gram_ids = ni;
        s->cap_grams = cap;
    }
    s->gram_paths[s->n_grams] = path;
    s->gram_ids[s->n_grams] = grammar;
    s->n_grams++;
    return 1;
}

/* Import binding records are collected but never resolved here: the
 * compat gate is a pure surface diff (see compat.h). */
static oc_code side_build(compat_side *s, const char *root, const char *packs_dir, oc_arena *a,
                          oc_status *st) {
    oc_pack_set packs;
    oc_packs_init(&packs, a);
    oc_code rc = oc_packs_load_dir(&packs, packs_dir, st);
    if (rc != OC_OK) {
        oc_packs_destroy(&packs);
        return rc;
    }
    const char *roots[1] = {root};
    rc = oc_discover_files(roots, 1, &packs, a, &s->files, NULL, st);
    if (rc != OC_OK) {
        oc_packs_destroy(&packs);
        return rc;
    }
    oc_graph_init(&s->graph, a);
    oc_parser *parser = oc_parser_new(a, st);
    if (!parser) {
        oc_packs_destroy(&packs);
        return st->code;
    }
    for (size_t i = 0; i < s->files.n; i++) {
        const oc_pack *pack = oc_packs_match_path(&packs, s->files.items[i]);
        if (!pack)
            continue;
        char *data = NULL;
        size_t len = 0;
        rc = oc_read_file(s->files.items[i], &data, &len, st);
        if (rc != OC_OK)
            break;
        /* SPRINT-21: the C/C++ header routing (parse facts + root
         * default) BEFORE the grammar is recorded, so the side record
         * and the parse see the same pack the scan would route. */
        pack = oc_packs_route_source(&packs, s->files.items[i], data, len,
                                     (const char *const *)s->files.items, s->files.n, a, NULL);
        if (!pack) {
            free(data);
            continue;
        }
        if (!side_record_grammar(s, s->files.items[i], pack->grammar)) {
            oc_packs_destroy(&packs);
            oc_parser_free(parser);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory recording file grammar");
            return OC_ERR_ALLOC;
        }
        oc_parser_input in;
        memset(&in, 0, sizeof(in));
        in.path = s->files.items[i];
        in.source = data;
        in.len = len;
        in.grammar = pack->grammar;
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.binds = &s->binds;
        out.exports = &s->exports;
        out.hints = &s->hints;
        rc = oc_parser_parse_file(parser, &in, &s->graph, &out, st);
        free(data);
        if (rc != OC_OK)
            break;
    }
    oc_packs_destroy(&packs);
    oc_parser_free(parser);
    return rc;
}

/* Baseline file path relative to the side's root: strips the root
 * prefix (and the separator); paths outside the root pass through. */
static const char *rel_path(const compat_side *s, const char *path) {
    if (s->root_len > 0 && strncmp(path, s->root, s->root_len) == 0) {
        const char *rest = path + s->root_len;
        if (*rest == '/')
            rest++;
        return rest;
    }
    return path;
}

static int export_present(const compat_side *s, const char *path, const char *name) {
    for (size_t i = 0; i < s->exports.n; i++) {
        if (strcmp(rel_path(s, s->exports.items[i].path), path) == 0 &&
            strcmp(s->exports.items[i].name, name) == 0)
            return 1;
    }
    return 0;
}

static int file_present(const compat_side *s, const char *path) {
    for (size_t i = 0; i < s->files.n; i++)
        if (strcmp(rel_path(s, s->files.items[i]), path) == 0)
            return 1;
    return 0;
}

static int function_present(const compat_side *s, const char *path, const char *name) {
    for (size_t i = 0; i < s->graph.n_nodes; i++) {
        const oc_node *n = &s->graph.nodes[i];
        if (n->kind == OC_NODE_FUNCTION && strcmp(rel_path(s, n->path), path) == 0 &&
            strcmp(n->name, name) == 0)
            return 1;
    }
    return 0;
}

static int path_has_exports(const compat_side *s, const char *path) {
    for (size_t i = 0; i < s->exports.n; i++)
        if (strcmp(rel_path(s, s->exports.items[i].path), path) == 0)
            return 1;
    return 0;
}

/* Change record for the report. detail is NULL for the v0 kinds and a
 * one-line old -> new summary for the SPRINT-17 signature kinds. */
typedef struct change {
    const char *kind;
    const char *path;
    const char *symbol; /* NULL for removed-module */
    const char *detail; /* NULL for v0 kinds */
} change;

static int change_cmp(const void *pa, const void *pb) {
    const change *a = (const change *)pa, *b = (const change *)pb;
    int c = strcmp(a->kind, b->kind);
    if (c)
        return c;
    c = strcmp(a->path, b->path);
    if (c)
        return c;
    const char *sa = a->symbol ? a->symbol : "";
    const char *sb = b->symbol ? b->symbol : "";
    c = strcmp(sa, sb);
    if (c)
        return c;
    const char *da = a->detail ? a->detail : "";
    const char *db = b->detail ? b->detail : "";
    return strcmp(da, db);
}

static void write_json_str(FILE *out, const char *s) {
    fputc('"', out);
    if (s) {
        for (const char *p = s; *p; p++) {
            if (*p == '"' || *p == '\\')
                fputc('\\', out);
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

/* ---- SPRINT-17 (B3): signature policy + diff -------------------------
 * Binding scope: docs/SPRINT-17.md sections 2-3 and the decision record
 * in docs/B3_COMPAT_SIGNATURE_DIFFS.md section 7 (Option B, typed-
 * where-declared). Raw type text only; NULL on either side is "not
 * comparable", never "equal". */

typedef enum cgram { CG_UNKNOWN = 0, CG_JS, CG_PY, CG_GO, CG_JAVA, CG_CS, CG_RUST, CG_RUBY } cgram;

static cgram grammar_family(const char *g) {
    if (!g)
        return CG_UNKNOWN;
    if (strcmp(g, "javascript") == 0 || strcmp(g, "typescript") == 0 || strcmp(g, "tsx") == 0)
        return CG_JS;
    if (strcmp(g, "python") == 0)
        return CG_PY;
    if (strcmp(g, "go") == 0)
        return CG_GO;
    if (strcmp(g, "java") == 0)
        return CG_JAVA;
    if (strcmp(g, "csharp") == 0)
        return CG_CS;
    if (strcmp(g, "rust") == 0)
        return CG_RUST;
    if (strcmp(g, "ruby") == 0)
        return CG_RUBY;
    return CG_UNKNOWN;
}

static const char *gram_of(const compat_side *s, const char *abs_path) {
    for (size_t i = 0; i < s->n_grams; i++)
        if (strcmp(s->gram_paths[i], abs_path) == 0)
            return s->gram_ids[i];
    return NULL;
}

/* The binding policy table. */
static int fam_has_defaults(cgram f) {
    return f == CG_JS || f == CG_PY || f == CG_RUBY || f == CG_CS;
}
static int fam_named_args(cgram f) {
    return f == CG_PY || f == CG_CS;
}
static int fam_overloads(cgram f) {
    return f == CG_JAVA || f == CG_CS;
}

/* NULL-safe equality (NULL == NULL is "same unknown"). */
static int type_same(const char *a, const char *b) {
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;
    return strcmp(a, b) == 0;
}
/* Comparable difference: BOTH sides declared, text differs. */
static int type_comparable_diff(const char *a, const char *b) {
    return a && b && strcmp(a, b) != 0;
}
static int param_same(const oc_param *x, const oc_param *y) {
    const char *xn = x->name ? x->name : "";
    const char *yn = y->name ? y->name : "";
    return strcmp(xn, yn) == 0 && type_same(x->type, y->type);
}

/* Canonical identity text: "name:type,name:type" (defaults excluded --
 * they are policy, not identity). */
static char *sig_render(oc_arena *a, const oc_node *n) {
    size_t need = 2;
    for (size_t i = 0; i < n->n_params; i++) {
        need += strlen(n->params[i].name) + 4;
        if (n->params[i].type)
            need += strlen(n->params[i].type);
    }
    char *buf = (char *)oc_arena_alloc(a, need);
    if (!buf)
        return NULL;
    size_t off = 0;
    for (size_t i = 0; i < n->n_params; i++) {
        int w = snprintf(buf + off, need - off, "%s%s%s%s", i ? "," : "", n->params[i].name,
                         n->params[i].type ? ":" : "", n->params[i].type ? n->params[i].type : "");
        if (w < 0 || (size_t)w >= need - off)
            return NULL;
        off += (size_t)w;
    }
    return buf;
}

typedef struct rendered_sig {
    const oc_node *n;
    const char *text;
} rendered_sig;

static int rendered_sig_cmp(const void *pa, const void *pb) {
    const rendered_sig *x = (const rendered_sig *)pa, *y = (const rendered_sig *)pb;
    return strcmp(x->text, y->text);
}

/* Compares two KNOWN same-length signatures. Returns 1 with a finding
 * (kind/detail/breaking), 0 when identical or not comparable. Kind
 * precedence per the contract: reorder > type > rename. */
static int sig_compare(oc_arena *a, cgram fam, const oc_node *b, const oc_node *c,
                       const char **kind, const char **detail, int *breaking) {
    size_t bn = b->n_params, cn = c->n_params;
    if (bn != cn) { /* unreachable for the single-signature caller; kept total */
        *kind = "changed-param-count";
        *detail = oc_arena_sprintf(a, "arity %zu -> %zu", bn, cn);
        *breaking = 1;
        return *detail != NULL;
    }
    int identical = 1;
    for (size_t i = 0; i < bn; i++)
        if (!param_same(&b->params[i], &c->params[i])) {
            identical = 0;
            break;
        }
    if (identical)
        return 0;
    /* reorder: same (name,type) multiset, different order */
    int multiset_eq = 1;
    for (size_t i = 0; i < bn && multiset_eq; i++) {
        int hit = 0;
        for (size_t j = 0; j < cn; j++)
            if (param_same(&b->params[i], &c->params[j])) {
                hit = 1;
                break;
            }
        if (!hit)
            multiset_eq = 0;
    }
    if (multiset_eq) {
        *kind = "param-reordered";
        *detail = oc_arena_sprintf(a, "parameter order changed");
        *breaking = 1;
        return *detail != NULL;
    }
    for (size_t i = 0; i < bn; i++) {
        if (type_comparable_diff(b->params[i].type, c->params[i].type)) {
            *kind = "changed-param-type";
            *detail = oc_arena_sprintf(a, "param %zu type: %s -> %s", i + 1, b->params[i].type,
                                       c->params[i].type);
            *breaking = 1;
            return *detail != NULL;
        }
    }
    for (size_t i = 0; i < bn; i++) {
        const char *bnm = b->params[i].name ? b->params[i].name : "";
        const char *cnm = c->params[i].name ? c->params[i].name : "";
        if (strcmp(bnm, cnm) != 0) {
            *kind = "param-renamed";
            *detail = oc_arena_sprintf(a, "param %zu name: %s -> %s", i + 1, bnm, cnm);
            *breaking = fam_named_args(fam);
            return *detail != NULL;
        }
    }
    /* Incomparable shift (e.g. declared type dropped): outside the
     * pre-committed policy -- no finding, honestly. */
    return 0;
}

static int push_change(change **arr, size_t *n, size_t *cap, const char *kind, const char *path,
                       const char *symbol, const char *detail) {
    if (*n == *cap) {
        size_t nc = *cap ? *cap * 2 : 16;
        change *na = (change *)realloc(*arr, nc * sizeof(change));
        if (!na)
            return 0;
        *arr = na;
        *cap = nc;
    }
    (*arr)[*n].kind = kind;
    (*arr)[*n].path = path;
    (*arr)[*n].symbol = symbol;
    (*arr)[*n].detail = detail;
    (*n)++;
    return 1;
}

/* Collects the function nodes of side `s` at rel path `path` with name
 * `name` (arena-allocated array of node pointers). */
static int collect_group(const compat_side *s, const char *path, const char *name,
                         const oc_node ***out, size_t *out_n, oc_arena *a) {
    size_t cnt = 0;
    for (size_t i = 0; i < s->graph.n_nodes; i++) {
        const oc_node *n = &s->graph.nodes[i];
        if (n->kind != OC_NODE_FUNCTION)
            continue;
        if (strcmp(rel_path(s, n->path), path) != 0 || strcmp(n->name, name) != 0)
            continue;
        cnt++;
    }
    const oc_node **items = NULL;
    if (cnt) {
        items = (const oc_node **)oc_arena_alloc(a, cnt * sizeof(*items));
        if (!items)
            return 0;
        size_t k = 0;
        for (size_t i = 0; i < s->graph.n_nodes; i++) {
            const oc_node *n = &s->graph.nodes[i];
            if (n->kind != OC_NODE_FUNCTION)
                continue;
            if (strcmp(rel_path(s, n->path), path) != 0 || strcmp(n->name, name) != 0)
                continue;
            items[k++] = n;
        }
    }
    *out = items;
    *out_n = cnt;
    return 1;
}

static int sig_has_arity(const oc_node *n, size_t arity) {
    return n->params != NULL && n->n_params == arity;
}

/* Emits the signature findings for one (path, name) group. Overload
 * families (Java, C#) compare per arity as a signature SET; every other
 * family pairs the first known baseline node with the first known
 * current node. Returns 0 on allocation failure. */
static int sig_diff_group(oc_arena *a, cgram fam, const compat_side *base, const compat_side *cur,
                          const char *path, const char *name, change **breaking, size_t *nb,
                          size_t *cb, change **info, size_t *ni, size_t *ci) {
    const oc_node **bgroup = NULL, **cgroup = NULL;
    size_t bn_group = 0, cn_group = 0;
    if (!collect_group(base, path, name, &bgroup, &bn_group, a))
        return 0;
    if (!collect_group(cur, path, name, &cgroup, &cn_group, a))
        return 0;
    if (bn_group == 0 || cn_group == 0)
        return 1; /* removal/addition handled by the v0 kinds */

    if (fam_overloads(fam)) {
        /* distinct baseline arities */
        for (size_t i = 0; i < bn_group; i++) {
            const oc_node *b = bgroup[i];
            if (b->params == NULL)
                continue;
            size_t A = b->n_params;
            int seen = 0;
            for (size_t j = 0; j < i; j++)
                if (bgroup[j]->params && bgroup[j]->n_params == A) {
                    seen = 1;
                    break;
                }
            if (seen)
                continue; /* arity already processed */
            size_t b_cnt = 0, c_cnt = 0;
            for (size_t j = 0; j < bn_group; j++)
                if (sig_has_arity(bgroup[j], A))
                    b_cnt++;
            for (size_t j = 0; j < cn_group; j++)
                if (sig_has_arity(cgroup[j], A))
                    c_cnt++;
            char *d = NULL;
            if (c_cnt == 0) {
                d = oc_arena_sprintf(a, "arity %zu signature set removed", A);
                if (!d)
                    return 0;
                if (!push_change(breaking, nb, cb, "changed-param-count", path, name, d))
                    return 0;
            } else if (c_cnt < b_cnt) {
                d = oc_arena_sprintf(a, "arity %zu overloads %zu -> %zu", A, b_cnt, c_cnt);
                if (!d)
                    return 0;
                if (!push_change(info, ni, ci, "removed-overload", path, name, d))
                    return 0;
            } else if (c_cnt > b_cnt) {
                d = oc_arena_sprintf(a, "arity %zu overloads %zu -> %zu", A, b_cnt, c_cnt);
                if (!d)
                    return 0;
                if (!push_change(info, ni, ci, "added-overload", path, name, d))
                    return 0;
            } else {
                /* same arity, same count: pair by canonical text */
                rendered_sig *bs = (rendered_sig *)oc_arena_alloc(a, b_cnt * sizeof(*bs));
                rendered_sig *cs = (rendered_sig *)oc_arena_alloc(a, c_cnt * sizeof(*cs));
                if (!bs || !cs)
                    return 0;
                size_t kb = 0, kc = 0;
                for (size_t j = 0; j < bn_group; j++)
                    if (sig_has_arity(bgroup[j], A)) {
                        bs[kb].n = bgroup[j];
                        bs[kb].text = sig_render(a, bgroup[j]);
                        if (!bs[kb].text)
                            return 0;
                        kb++;
                    }
                for (size_t j = 0; j < cn_group; j++)
                    if (sig_has_arity(cgroup[j], A)) {
                        cs[kc].n = cgroup[j];
                        cs[kc].text = sig_render(a, cgroup[j]);
                        if (!cs[kc].text)
                            return 0;
                        kc++;
                    }
                qsort(bs, kb, sizeof(*bs), rendered_sig_cmp);
                qsort(cs, kc, sizeof(*cs), rendered_sig_cmp);
                for (size_t j = 0; j < kb; j++) {
                    const char *kind = NULL, *detail = NULL;
                    int is_breaking = 0;
                    if (sig_compare(a, fam, bs[j].n, cs[j].n, &kind, &detail, &is_breaking)) {
                        if (!push_change(is_breaking ? breaking : info, is_breaking ? nb : ni,
                                         is_breaking ? cb : ci, kind, path, name, detail))
                            return 0;
                    }
                }
            }
        }
        /* brand-new arities on the current side */
        for (size_t i = 0; i < cn_group; i++) {
            const oc_node *c = cgroup[i];
            if (c->params == NULL)
                continue;
            size_t A = c->n_params;
            int in_base = 0, seen = 0;
            for (size_t j = 0; j < bn_group; j++)
                if (sig_has_arity(bgroup[j], A)) {
                    in_base = 1;
                    break;
                }
            for (size_t j = 0; j < i; j++)
                if (cgroup[j]->params && cgroup[j]->n_params == A) {
                    seen = 1;
                    break;
                }
            if (in_base || seen)
                continue;
            char *d = oc_arena_sprintf(a, "arity %zu added", A);
            if (!d)
                return 0;
            if (!push_change(info, ni, ci, "added-overload", path, name, d))
                return 0;
        }
        return 1;
    }

    /* single-signature family: first KNOWN node on each side */
    const oc_node *b = NULL, *c = NULL;
    for (size_t i = 0; i < bn_group; i++)
        if (bgroup[i]->params) {
            b = bgroup[i];
            break;
        }
    for (size_t i = 0; i < cn_group; i++)
        if (cgroup[i]->params) {
            c = cgroup[i];
            break;
        }
    if (!b || !c)
        return 1; /* signature unknown on one side: honestly not comparable */
    if (b->n_params != c->n_params) {
        int is_breaking = 1;
        const char *kind = "changed-param-count";
        char *d = NULL;
        if (c->n_params > b->n_params && fam_has_defaults(fam)) {
            int all_def = 1;
            for (size_t i = b->n_params; i < c->n_params; i++)
                if (!c->params[i].has_default) {
                    all_def = 0;
                    break;
                }
            if (all_def) {
                kind = "added-param-default";
                is_breaking = 0;
            }
        }
        d = oc_arena_sprintf(a, "arity %zu -> %zu", b->n_params, c->n_params);
        if (!d)
            return 0;
        return push_change(is_breaking ? breaking : info, is_breaking ? nb : ni,
                           is_breaking ? cb : ci, kind, path, name, d);
    }
    const char *kind = NULL, *detail = NULL;
    int is_breaking = 0;
    if (sig_compare(a, fam, b, c, &kind, &detail, &is_breaking))
        return push_change(is_breaking ? breaking : info, is_breaking ? nb : ni,
                           is_breaking ? cb : ci, kind, path, name, detail);
    return 1;
}

oc_code oc_compat_run(const char *baseline_root, const char *current_root, const char *packs_dir,
                      FILE *out, oc_status *st) {
    oc_arena arena;
    oc_arena_init(&arena);
    compat_side base, cur;
    side_init(&base);
    side_init(&cur);
    /* Declared BEFORE any goto: the early-failure paths reach `done:`
     * and must see these initialized (GCC maybe-uninitialized). */
    change *breaking = NULL, *info = NULL;
    size_t cap_breaking = 0, cap_info = 0;
    size_t n_breaking = 0, n_info = 0;
    oc_code rc = OC_OK;

    rc = side_build(&base, baseline_root, packs_dir, &arena, st);
    if (rc != OC_OK)
        goto done;
    base.root = baseline_root;
    base.root_len = strlen(baseline_root);
    rc = side_build(&cur, current_root, packs_dir, &arena, st);
    if (rc != OC_OK)
        goto done;
    cur.root = current_root;
    cur.root_len = strlen(current_root);

    /* Build the change lists (dynamically grown: the SPRINT-17
     * signature findings are not bounded by either side alone). All
     * report paths are baseline-root-relative (see rel_path). */

    /* Baseline files absent from the current tree. */
    for (size_t i = 0; i < base.files.n; i++) {
        const char *path = rel_path(&base, base.files.items[i]);
        if (file_present(&cur, path))
            continue;
        int was_public = path_has_exports(&base, path);
        if (was_public) {
            if (!push_change(&breaking, &n_breaking, &cap_breaking, "removed-module", path, NULL,
                             NULL))
                goto alloc_fail;
        } else {
            if (!push_change(&info, &n_info, &cap_info, "removed-module", path, NULL, NULL))
                goto alloc_fail;
        }
    }
    /* Baseline exports absent from the current tree (same path). */
    for (size_t i = 0; i < base.exports.n; i++) {
        const oc_export_rec *e = &base.exports.items[i];
        const char *path = rel_path(&base, e->path);
        if (file_present(&cur, path) && !export_present(&cur, path, e->name)) {
            if (!push_change(&breaking, &n_breaking, &cap_breaking, "removed-export", path, e->name,
                             NULL))
                goto alloc_fail;
        }
    }
    /* Function nodes gone from public modules that still exist. */
    for (size_t i = 0; i < base.graph.n_nodes; i++) {
        const oc_node *n = &base.graph.nodes[i];
        if (n->kind != OC_NODE_FUNCTION)
            continue;
        const char *path = rel_path(&base, n->path);
        if (!file_present(&cur, path))
            continue; /* module removal is reported once, above */
        if (!path_has_exports(&base, path))
            continue; /* private module: internal refactor */
        if (!function_present(&cur, path, n->name)) {
            if (!push_change(&breaking, &n_breaking, &cap_breaking, "removed-function", path,
                             n->name, NULL))
                goto alloc_fail;
        }
    }
    /* Informational additions (bounded: current side only). */
    for (size_t i = 0; i < cur.exports.n; i++) {
        const oc_export_rec *e = &cur.exports.items[i];
        const char *path = rel_path(&cur, e->path);
        if (!file_present(&base, path) || !export_present(&base, path, e->name)) {
            if (!push_change(&info, &n_info, &cap_info, "added-export", path, e->name, NULL))
                goto alloc_fail;
        }
    }
    for (size_t i = 0; i < base.files.n; i++) {
        const char *bpath = rel_path(&base, base.files.items[i]);
        if (!file_present(&cur, bpath))
            continue;
        /* functions added to files present in both */
        for (size_t j = 0; j < cur.graph.n_nodes; j++) {
            const oc_node *n = &cur.graph.nodes[j];
            if (n->kind != OC_NODE_FUNCTION || strcmp(rel_path(&cur, n->path), bpath) != 0)
                continue;
            if (!function_present(&base, bpath, n->name)) {
                if (!push_change(&info, &n_info, &cap_info, "added-function", bpath, n->name, NULL))
                    goto alloc_fail;
            }
        }
    }
    for (size_t i = 0; i < cur.files.n; i++) {
        const char *path = rel_path(&cur, cur.files.items[i]);
        if (!file_present(&base, path)) {
            if (!push_change(&info, &n_info, &cap_info, "added-module", path, NULL, NULL))
                goto alloc_fail;
        }
    }

    /* SPRINT-17 (B3): signature diff on the public surface present on
     * both sides (same gate as removed-function: file present in both,
     * baseline side carries exports). One pass per (path, name) group. */
    for (size_t i = 0; i < base.graph.n_nodes; i++) {
        const oc_node *n = &base.graph.nodes[i];
        if (n->kind != OC_NODE_FUNCTION)
            continue;
        const char *path = rel_path(&base, n->path);
        if (!file_present(&cur, path))
            continue;
        if (!path_has_exports(&base, path))
            continue;
        if (!function_present(&cur, path, n->name))
            continue;
        int earlier = 0;
        for (size_t j = 0; j < i && !earlier; j++) {
            const oc_node *m = &base.graph.nodes[j];
            if (m->kind == OC_NODE_FUNCTION && strcmp(rel_path(&base, m->path), path) == 0 &&
                strcmp(m->name, n->name) == 0)
                earlier = 1;
        }
        if (earlier)
            continue;
        cgram fam = grammar_family(gram_of(&base, n->path));
        if (fam == CG_UNKNOWN)
            continue;
        if (!sig_diff_group(&arena, fam, &base, &cur, path, n->name, &breaking, &n_breaking,
                            &cap_breaking, &info, &n_info, &cap_info))
            goto alloc_fail;
    }

    qsort(breaking, n_breaking, sizeof(change), change_cmp);
    qsort(info, n_info, sizeof(change), change_cmp);
    int is_breaking = n_breaking > 0;

    fprintf(out, "{\n  \"schema\": \"omnicheck.compat_report/v1\",\n  \"baseline\": ");
    write_json_str(out, baseline_root);
    fprintf(out, ",\n  \"current\": ");
    write_json_str(out, current_root);
    fprintf(out, ",\n  \"verdict\": \"%s\",\n  \"breaking\": [", is_breaking ? "BREAKING" : "SAFE");
    for (size_t i = 0; i < n_breaking; i++) {
        fprintf(out, "%s\n    {\"kind\": \"%s\", \"path\": ", i ? "," : "", breaking[i].kind);
        write_json_str(out, breaking[i].path);
        fprintf(out, ", \"symbol\": ");
        write_json_str(out, breaking[i].symbol);
        if (breaking[i].detail) {
            fprintf(out, ", \"detail\": ");
            write_json_str(out, breaking[i].detail);
        }
        fputc('}', out);
    }
    fprintf(out, "\n  ],\n  \"informational\": [");
    for (size_t i = 0; i < n_info; i++) {
        fprintf(out, "%s\n    {\"kind\": \"%s\", \"path\": ", i ? "," : "", info[i].kind);
        write_json_str(out, info[i].path);
        fprintf(out, ", \"symbol\": ");
        write_json_str(out, info[i].symbol);
        if (info[i].detail) {
            fprintf(out, ", \"detail\": ");
            write_json_str(out, info[i].detail);
        }
        fputc('}', out);
    }
    fprintf(out, "\n  ],\n  \"counts\": {\"breaking\": %zu, \"informational\": %zu}\n}\n",
            n_breaking, n_info);

    rc = is_breaking ? 1 : 0;
    oc_status_clear(st);

done:
    free(breaking);
    free(info);
    side_destroy(&base);
    side_destroy(&cur);
    oc_arena_destroy(&arena);
    return rc;

alloc_fail:
    free(breaking);
    free(info);
    oc_status_set(st, OC_ERR_ALLOC, "out of memory building compat diff");
    rc = OC_ERR_ALLOC;
    goto done;
}
