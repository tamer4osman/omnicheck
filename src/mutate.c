#include "oc/mutate.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "oc/coverage.h"
#include "oc/discover.h"
#include "oc/graph.h"
#include "oc/json.h"
#include "oc/pack.h"
#include "oc/parser.h"
#include "parser_internal.h"

#if !defined(OC_WITH_TREE_SITTER)
/* The engine is plan-scoped to tree-sitter operator sets; a build
 * without the backend has no grammar to compile queries against. */
oc_code oc_mutate_collect(const char *grammar, const char *path, const char *src, size_t len,
                          oc_arena *a, oc_mutant_vec *out, oc_status *st) {
    (void)grammar;
    (void)path;
    (void)src;
    (void)len;
    (void)a;
    (void)out;
    oc_status_set(st, OC_ERR_INTERNAL, "mutation engine requires the tree-sitter backend");
    return OC_ERR_INTERNAL;
}
#else

/* ---------------------------------------------------- operator sets --- */

typedef struct op_pair {
    const char *from, *to;
} op_pair;

/* Plan Ch4 4.6 operator sets, mapped per grammar. */
static const op_pair REL_PAIRS_JS[] = {{"<", ">="},    {">=", "<"},    {">", "<="},
                                       {"<=", ">"},    {"==", "!="},   {"!=", "=="},
                                       {"===", "!=="}, {"!==", "==="}, {NULL, NULL}};
static const op_pair LOGIC_PAIRS_JS[] = {{"&&", "||"}, {"||", "&&"}, {NULL, NULL}};
static const op_pair REL_PAIRS_PY[] = {{"<", ">="},  {">=", "<"},  {">", "<="}, {"<=", ">"},
                                       {"==", "!="}, {"!=", "=="}, {NULL, NULL}};
static const op_pair LOGIC_PAIRS_PY[] = {{"and", "or"}, {"or", "and"}, {NULL, NULL}};

static const char *Q_MUT_JS = "(binary_expression) @bin\n"
                              "(unary_expression operator: \"!\") @neg\n"
                              "(expression_statement) @stmt\n"
                              "(return_statement) @ret\n"
                              "(call_expression arguments: (arguments) @args)\n"
                              "(true) @bool\n"
                              "(false) @bool\n"
                              "(number) @num\n";

static const char *Q_MUT_PY = "(comparison_operator) @bin\n"
                              "(boolean_operator) @bin\n"
                              "(not_operator) @neg\n"
                              "(expression_statement) @stmt\n"
                              "(return_statement) @ret\n"
                              "(call arguments: (argument_list) @args)\n"
                              "(true) @bool\n"
                              "(false) @bool\n"
                              "(integer) @num\n";

static const op_pair *rel_pairs_for(int family) {
    return family == 0 ? REL_PAIRS_JS : REL_PAIRS_PY;
}
static const op_pair *logic_pairs_for(int family) {
    return family == 0 ? LOGIC_PAIRS_JS : LOGIC_PAIRS_PY;
}
static const char *ret_replacement_for(int family) {
    return family == 0 ? "0" : "None";
}

/* --------------------------------------------------------- vec ops --- */

void oc_mutants_init(oc_mutant_vec *v) {
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}

void oc_mutants_destroy(oc_mutant_vec *v) {
    free(v->items);
    oc_mutants_init(v);
}

static int mutant_push(oc_mutant_vec *v, oc_mutant m) {
    if (v->n == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 16;
        oc_mutant *grown = (oc_mutant *)realloc(v->items, ncap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = ncap;
    }
    v->items[v->n++] = m;
    return 1;
}

static oc_mut_status string_to_status(const char *s) {
    if (!s)
        return OC_MUT_PENDING;
    if (strcmp(s, "killed") == 0)
        return OC_MUT_KILLED;
    if (strcmp(s, "survived") == 0)
        return OC_MUT_SURVIVED;
    if (strcmp(s, "timeout") == 0)
        return OC_MUT_TIMEOUT;
    return OC_MUT_PENDING;
}

const char *oc_mut_status_str(oc_mut_status s) {
    switch (s) {
    case OC_MUT_KILLED:
        return "killed";
    case OC_MUT_SURVIVED:
        return "survived";
    case OC_MUT_TIMEOUT:
        return "timeout";
    case OC_MUT_SKIPPED_CACHE:
        return "skipped-cache";
    case OC_MUT_SKIPPED_COV:
        return "skipped-coverage";
    case OC_MUT_SKIPPED_BUDGET:
        return "skipped-budget";
    case OC_MUT_PENDING:
    default:
        return "pending";
    }
}

/* ------------------------------------------------------ collection --- */

static int is_rel_or_logic(int family, const char *text, const char **out_replacement) {
    for (const op_pair *p = rel_pairs_for(family); p->from; p++) {
        if (strcmp(text, p->from) == 0) {
            *out_replacement = p->to;
            return 1;
        }
    }
    for (const op_pair *p = logic_pairs_for(family); p->from; p++) {
        if (strcmp(text, p->from) == 0) {
            *out_replacement = p->to;
            return 1;
        }
    }
    return 0;
}

static uint32_t node_line(const char *src, size_t len, uint32_t byte_off) {
    uint32_t line = 1;
    for (uint32_t i = 0; i < len && i < byte_off; i++)
        if (src[i] == '\n')
            line++;
    return line;
}

/* FNV-1a 64 over a byte range: cache-key material. */
static uint64_t fnv1a(const char *data, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int mutant_add(oc_mutant_vec *out, oc_arena *a, const char *path, const char *src,
                      size_t len, uint32_t start, uint32_t end, const char *op_id,
                      const char *replacement) {
    /* dedup: identical edit at identical bytes from overlapping captures */
    for (size_t i = 0; i < out->n; i++) {
        const oc_mutant *m = &out->items[i];
        if (m->byte_start == start && m->byte_end == end &&
            strcmp(m->replacement, replacement) == 0)
            return 1;
    }
    oc_mutant m;
    memset(&m, 0, sizeof(m));
    m.file = oc_arena_strdup(a, path);
    m.byte_start = start;
    m.byte_end = end;
    m.line = node_line(src, len, start);
    m.col = 1;
    for (uint32_t i = start; i > 0 && i < len; i--) {
        if (src[i - 1] == '\n') {
            m.col = start - i + 2;
            break;
        }
    }
    m.op_id = oc_arena_strdup(a, op_id);
    m.original = oc_arena_strndup(a, src + start, end - start);
    m.replacement = oc_arena_strdup(a, replacement);
    m.content_hash = fnv1a(src, len);
    if (!m.file || !m.op_id || !m.original || !m.replacement)
        return 0;
    return mutant_push(out, m);
}

oc_code oc_mutate_collect(const char *grammar, const char *path, const char *src, size_t len,
                          oc_arena *a, oc_mutant_vec *out, oc_status *st) {
    int family = -1;
    if (strcmp(grammar, "python") == 0)
        family = 1;
    else if (strcmp(grammar, "javascript") == 0 || strcmp(grammar, "typescript") == 0 ||
             strcmp(grammar, "tsx") == 0)
        family = 0;
    else {
        oc_status_set(st, OC_ERR_USAGE, "grammar '%s' has no mutation operator set", grammar);
        return OC_ERR_USAGE;
    }
    const TSLanguage *lang = oc_ts_language_for(grammar);
    if (!lang) {
        oc_status_set(st, OC_ERR_INTERNAL, "no language for grammar '%s'", grammar);
        return OC_ERR_INTERNAL;
    }
    const char *qsrc = family == 0 ? Q_MUT_JS : Q_MUT_PY;
    TSQueryError qerr = TSQueryErrorNone;
    uint32_t qerr_off = 0;
    TSQuery *q = ts_query_new(lang, qsrc, (uint32_t)strlen(qsrc), &qerr_off, &qerr);
    if (!q) {
        oc_status_set(st, OC_ERR_INTERNAL, "mutation query failed for '%s' (err=%d at %u)", grammar,
                      (int)qerr, qerr_off);
        return OC_ERR_INTERNAL;
    }
    TSParser *parser = ts_parser_new();
    oc_code rc = OC_OK;
    TSTree *tree = NULL;
    TSQueryCursor *cur = NULL;
    if (!ts_parser_set_language(parser, lang)) {
        oc_status_set(st, OC_ERR_INTERNAL, "grammar ABI mismatch for '%s' (mutation)", grammar);
        rc = OC_ERR_INTERNAL;
        goto done;
    }
    tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)len);
    if (!tree) {
        oc_status_set(st, OC_ERR_PARSE, "parse failed during mutation collection for '%s'", path);
        rc = OC_ERR_PARSE;
        goto done;
    }
    cur = ts_query_cursor_new();
    ts_query_cursor_exec(cur, q, ts_tree_root_node(tree));
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cur, &match)) {
        for (uint32_t c = 0; c < match.capture_count; c++) {
            uint32_t idx = match.captures[c].index;
            uint32_t name_len = 0;
            const char *name = ts_query_capture_name_for_id(q, idx, &name_len);
            TSNode node = match.captures[c].node;
            uint32_t ns = ts_node_start_byte(node), ne = ts_node_end_byte(node);
            if (name_len == 3 && strncmp(name, "bin", 3) == 0) {
                uint32_t nch = ts_node_child_count(node);
                for (uint32_t k = 0; k < nch; k++) {
                    TSNode ch = ts_node_child(node, k);
                    if (ts_node_is_named(ch))
                        continue;
                    uint32_t cs = ts_node_start_byte(ch), ce = ts_node_end_byte(ch);
                    const char *tok = src + cs;
                    size_t tlen = ce - cs;
                    char buf[8];
                    if (tlen == 0 || tlen >= sizeof(buf))
                        continue;
                    memcpy(buf, tok, tlen);
                    buf[tlen] = '\0';
                    const char *repl = NULL;
                    if (is_rel_or_logic(family, buf, &repl)) {
                        if (!mutant_add(out, a, path, src, len, cs, ce,
                                        strchr(buf, '=') || strchr(buf, '<') || strchr(buf, '>')
                                            ? "rel-swap"
                                            : "logic-swap",
                                        repl)) {
                            rc = OC_ERR_ALLOC;
                            goto done;
                        }
                    }
                }
            } else if (name_len == 3 && strncmp(name, "neg", 3) == 0) {
                /* delete the negation token ('!' / 'not') */
                uint32_t nch = ts_node_child_count(node);
                for (uint32_t k = 0; k < nch; k++) {
                    TSNode ch = ts_node_child(node, k);
                    if (ts_node_is_named(ch))
                        continue;
                    uint32_t cs = ts_node_start_byte(ch), ce = ts_node_end_byte(ch);
                    size_t tlen = ce - cs;
                    if (tlen != 1 && tlen != 3)
                        continue; /* '!' (1 byte) or 'not' (3 bytes) */
                    if (src[cs] != '!' && memcmp(src + cs, "not", 3) != 0)
                        continue;
                    if (!mutant_add(out, a, path, src, len, cs, ce, "negate-flip", "")) {
                        rc = OC_ERR_ALLOC;
                        goto done;
                    }
                    break;
                }
            } else if (name_len == 4 && strncmp(name, "stmt", 4) == 0) {
                /* Skip bare-string expression statements (module/class/
                 * function docstrings): deleting a statement that can
                 * never have an effect is an equivalent mutant and would
                 * pollute the survival report with noise. */
                int is_bare_string = 0;
                if (family == 1) {
                    uint32_t nch = ts_node_child_count(node);
                    for (uint32_t k = 0; k < nch; k++) {
                        TSNode ch = ts_node_child(node, k);
                        if (!ts_node_is_named(ch))
                            continue;
                        const char *t = ts_node_type(ch);
                        is_bare_string = strcmp(t, "string") == 0;
                        break;
                    }
                }
                if (!is_bare_string &&
                    !mutant_add(out, a, path, src, len, ns, ne, "stmt-del", "")) {
                    rc = OC_ERR_ALLOC;
                    goto done;
                }
            } else if (name_len == 3 && strncmp(name, "ret", 3) == 0) {
                /* replace the first named child (the returned expression) */
                uint32_t nch = ts_node_child_count(node);
                for (uint32_t k = 0; k < nch; k++) {
                    TSNode ch = ts_node_child(node, k);
                    if (!ts_node_is_named(ch))
                        continue;
                    uint32_t cs = ts_node_start_byte(ch), ce = ts_node_end_byte(ch);
                    if (!mutant_add(out, a, path, src, len, cs, ce, "ret-change",
                                    ret_replacement_for(family))) {
                        rc = OC_ERR_ALLOC;
                        goto done;
                    }
                    break;
                }
            } else if (name_len == 4 && strncmp(name, "args", 4) == 0) {
                /* swap the first two argument expressions */
                TSNode a0 = ts_node_child(node, 0);
                uint32_t nch = ts_node_child_count(node);
                TSNode first = {0}, second = {0};
                int got = 0;
                for (uint32_t k = 0; k < nch; k++) {
                    TSNode ch = ts_node_child(node, k);
                    if (!ts_node_is_named(ch))
                        continue;
                    if (got == 0) {
                        first = ch;
                        got = 1;
                    } else {
                        second = ch;
                        got = 2;
                        break;
                    }
                }
                if (got == 2) {
                    (void)a0;
                    uint32_t s0 = ts_node_start_byte(first), e0 = ts_node_end_byte(first);
                    uint32_t s1 = ts_node_start_byte(second), e1 = ts_node_end_byte(second);
                    if (e0 <= s1) { /* ordered; no pathological nesting */
                        size_t len0 = e0 - s0, len1 = e1 - s1;
                        char *repl = (char *)oc_arena_alloc(a, len1 + 2 + len0 + 1);
                        if (!repl) {
                            rc = OC_ERR_ALLOC;
                            goto done;
                        }
                        memcpy(repl, src + s1, len1);
                        memcpy(repl + len1, ", ", 2);
                        memcpy(repl + len1 + 2, src + s0, len0);
                        repl[len1 + 2 + len0] = '\0';
                        if (!mutant_add(out, a, path, src, len, s0, e1, "arg-swap", repl)) {
                            rc = OC_ERR_ALLOC;
                            goto done;
                        }
                    }
                }
            } else if (name_len == 4 && strncmp(name, "bool", 4) == 0) {
                uint32_t tlen = ne - ns;
                const char *txt = src + ns;
                const char *repl = NULL;
                if (tlen == 4 && memcmp(txt, "true", 4) == 0)
                    repl = "false";
                else if (tlen == 5 && memcmp(txt, "false", 5) == 0)
                    repl = "true";
                else if (tlen == 4 && memcmp(txt, "True", 4) == 0)
                    repl = "False";
                else if (tlen == 5 && memcmp(txt, "False", 5) == 0)
                    repl = "True";
                if (repl && !mutant_add(out, a, path, src, len, ns, ne, "const-tweak", repl)) {
                    rc = OC_ERR_ALLOC;
                    goto done;
                }
            } else if (name_len == 3 && strncmp(name, "num", 3) == 0) {
                uint32_t tlen = ne - ns;
                char buf[32];
                if (tlen == 0 || tlen >= sizeof(buf))
                    continue;
                memcpy(buf, src + ns, tlen);
                buf[tlen] = '\0';
                if (strchr(buf, '.') || strchr(buf, 'x') || strchr(buf, 'X') || strchr(buf, 'e') ||
                    strchr(buf, 'E'))
                    continue; /* floats/hex/exponents: skipped, never guessed */
                long long v = strtoll(buf, NULL, 10);
                char repl[32];
                snprintf(repl, sizeof(repl), "%lld", v + 1);
                if (!mutant_add(out, a, path, src, len, ns, ne, "const-tweak", repl)) {
                    rc = OC_ERR_ALLOC;
                    goto done;
                }
            }
        }
    }

done:
    if (cur)
        ts_query_cursor_delete(cur);
    if (tree)
        ts_tree_delete(tree);
    ts_parser_delete(parser);
    ts_query_delete(q);
    if (rc == OC_OK)
        oc_status_clear(st);
    return rc;
}

/* --------------------------------------------------------- runner --- */

/* Executes the suite command in the current working directory with a
 * hard timeout: /bin/sh -c, own process group, deadline kill. Returns
 * the mutant status. Never inherits the analysis process's failure. */
static oc_mut_status run_test(const char *cmd, long timeout_s) {
    pid_t pid = fork();
    if (pid < 0)
        return OC_MUT_KILLED; /* fork failure: treat conservatively as killed */
    if (pid == 0) {
        setpgid(0, 0);
        /* the suite's own output is not part of the mutation report
         * contract: silence it so the JSON report stays byte-clean */
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid); /* also from the parent, race-safe */
    long elapsed_ms = 0;
    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            break;
        if (r < 0) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            return OC_MUT_KILLED;
        }
        if (elapsed_ms >= timeout_s * 1000L) {
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            return OC_MUT_TIMEOUT;
        }
        struct timespec ts = {0, 50 * 1000 * 1000}; /* 50 ms poll */
        nanosleep(&ts, NULL);
        elapsed_ms += 50;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status) == 0 ? OC_MUT_SURVIVED : OC_MUT_KILLED;
    return OC_MUT_KILLED; /* signaled */
}

static int mutant_cmp(const void *pa, const void *pb) {
    const oc_mutant *a = (const oc_mutant *)pa, *b = (const oc_mutant *)pb;
    if (a->byte_start != b->byte_start)
        return a->byte_start < b->byte_start ? -1 : 1;
    int c = strcmp(a->op_id, b->op_id);
    if (c)
        return c;
    if (a->byte_end != b->byte_end)
        return a->byte_end < b->byte_end ? -1 : 1;
    return strcmp(a->replacement, b->replacement);
}

/* Cache: {"omnicheck.mutation_cache/v0","entries":{key:status}} */
typedef struct cache_entry {
    char key[96];
    char status[16];
} cache_entry;

typedef struct cache_map {
    cache_entry *items;
    size_t n, cap;
} cache_map;

static void cache_map_init(cache_map *m) {
    memset(m, 0, sizeof(*m));
}
static void cache_map_free(cache_map *m) {
    free(m->items);
    cache_map_init(m);
}
static const char *cache_map_get(cache_map *m, const char *key) {
    for (size_t i = 0; i < m->n; i++)
        if (strcmp(m->items[i].key, key) == 0)
            return m->items[i].status;
    return NULL;
}
static int cache_map_put(cache_map *m, const char *key, const char *status) {
    if (cache_map_get(m, key))
        return 1;
    if (m->n == m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : 32;
        cache_entry *g = (cache_entry *)realloc(m->items, ncap * sizeof(*g));
        if (!g)
            return 0;
        m->items = g;
        m->cap = ncap;
    }
    snprintf(m->items[m->n].key, sizeof(m->items[m->n].key), "%s", key);
    snprintf(m->items[m->n].status, sizeof(m->items[m->n].status), "%s", status);
    m->n++;
    return 1;
}

static oc_code cache_load(cache_map *m, const char *path, oc_arena *a, oc_status *st) {
    char *data = NULL;
    size_t len = 0;
    oc_code rc = oc_read_file(path, &data, &len, st);
    if (rc != OC_OK)
        return rc; /* caller treats "no file" as fresh */
    oc_json *root = oc_json_parse(a, data, len, st);
    free(data);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "mutation cache '%s' is not a JSON object", path);
        return OC_ERR_USAGE;
    }
    const oc_json *entries = oc_json_get(root, "entries");
    if (!entries || entries->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "mutation cache '%s' lacks \"entries\"", path);
        return OC_ERR_USAGE;
    }
    for (size_t i = 0; i < entries->n_fields; i++)
        cache_map_put(m, entries->keys[i], oc_json_as_str(entries->vals[i], ""));
    return OC_OK;
}

/* Fully-escaped JSON string emission (RFC 8259: control chars < 0x20
 * must never appear raw inside a string). The report printer and the
 * cache writer share it. The previous hand-rolled loops escaped only
 * quote, backslash and newline -- source snippets containing TABs
 * (e.g. eslint's indentation) leaked literal 0x09 bytes into the
 * report and every strict JSON parser rejected it (caught by the
 * mutation budget re-measure on the pinned eslint repo, 2026-11-19). */
static void mut_json_string(FILE *out, const char *s) {
    fputc('"', out);
    for (const char *p = s; p && *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':
        case '\\':
            fputc('\\', out);
            fputc(c, out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        default:
            if (c < 0x20)
                fprintf(out, "\\u%04x", c);
            else
                fputc(c, out);
        }
    }
    fputc('"', out);
}

static oc_code cache_save(const cache_map *m, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return OC_ERR_IO;
    fprintf(f, "{\"schema\": \"omnicheck.mutation_cache/v0\", \"entries\": {");
    for (size_t i = 0; i < m->n; i++) {
        if (i)
            fputc(',', f);
        fputs("\n    ", f);
        mut_json_string(f, m->items[i].key);
        fputs(": ", f);
        mut_json_string(f, m->items[i].status);
    }
    fprintf(f, "\n  }\n}\n");
    fclose(f);
    return OC_OK;
}

/* Minimal graph build for the coverage-guided filter: discover + parse
 * (the pipeline's front half); import resolution is irrelevant here. */
static oc_code build_graph(const oc_mutate_options *o, oc_arena *a, oc_graph *g, oc_path_vec *files,
                           oc_status *st) {
    oc_pack_set packs;
    oc_packs_init(&packs, a);
    oc_code rc = oc_packs_load_dir(&packs, o->packs_dir, st);
    if (rc != OC_OK) {
        oc_packs_destroy(&packs);
        return rc;
    }
    rc = oc_discover_files(o->paths, o->n_paths, &packs, a, files, NULL, st);
    if (rc != OC_OK) {
        oc_packs_destroy(&packs);
        return rc;
    }
    oc_parser *parser = oc_parser_new(a, st);
    if (!parser) {
        oc_packs_destroy(&packs);
        return st->code;
    }
    for (size_t i = 0; i < files->n; i++) {
        const oc_pack *pack = oc_packs_match_path(&packs, files->items[i]);
        if (!pack)
            continue;
        char *data = NULL;
        size_t len = 0;
        rc = oc_read_file(files->items[i], &data, &len, st);
        if (rc != OC_OK)
            break;
        /* SPRINT-21: the C/C++ header routing -- one routing decision per
         * file everywhere (the scan's grammar choice is authoritative). */
        pack = oc_packs_route_source(&packs, files->items[i], data, len,
                                     (const char *const *)files->items, files->n, a, NULL);
        if (!pack) {
            free(data);
            continue;
        }
        oc_parser_input in;
        memset(&in, 0, sizeof(in));
        in.path = files->items[i];
        in.source = data;
        in.len = len;
        in.grammar = pack->grammar;
        rc = oc_parser_parse_file(parser, &in, g, NULL, st);
        free(data);
        if (rc != OC_OK)
            break;
    }
    oc_packs_destroy(&packs);
    oc_parser_free(parser);
    return rc;
}

/* True when the mutant's line lies inside a function whose composed
 * coverage is > 0 percent (test-reachable), when coverage was given. */
static int line_is_reachable(const oc_graph *g, const oc_coverage *cov, const char *path,
                             size_t line) {
    for (size_t i = 0; i < g->n_nodes; i++) {
        const oc_node *n = &g->nodes[i];
        if (n->kind != OC_NODE_FUNCTION || strcmp(n->path, path) != 0)
            continue;
        if (line < n->def_start_line || line > n->def_end_line)
            continue;
        size_t anchors = 0, covered = 0;
        if (!oc_coverage_lookup(cov, n->path, n->def_start_line, n->def_end_line, &anchors,
                                &covered))
            continue; /* file unmeasured: not test-reachable */
        if (anchors == 0)
            continue;
        if (covered > 0)
            return 1;
    }
    return 0;
}

/* basename starts with "test" or carries ".test." / "_test." -- the
 * documented test-file convention (mutation never mutates tests). */
static int is_test_path(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strncmp(base, "test", 4) == 0)
        return 1;
    return strstr(base, ".test.") != NULL || strstr(base, "_test.") != NULL;
}

oc_code oc_mutate_run(const oc_mutate_options *o, oc_mutate_report *rep, FILE *out, oc_status *st) {
    if (!o->test_cmd || !o->test_cmd[0]) {
        oc_status_set(st, OC_ERR_USAGE,
                      "--test-cmd is required: the engine never guesses how to run a suite");
        return OC_ERR_USAGE;
    }
    long timeout = o->timeout_s > 0 ? o->timeout_s : 30;
    memset(rep, 0, sizeof(*rep));
    oc_mutants_init(&rep->mutants);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    oc_arena arena;
    oc_arena_init(&arena);
    oc_code rc = OC_OK;
    oc_graph graph;
    oc_graph_init(&graph, &arena);
    oc_path_vec files;
    oc_paths_init(&files);
    oc_coverage cov;
    oc_coverage_init(&cov);
    int have_cov = 0;
    cache_map cache;
    cache_map_init(&cache);
    int have_cache = 0;

    rc = build_graph(o, &arena, &graph, &files, st);
    if (rc != OC_OK)
        goto done;

    if (o->n_coverage > 0) {
        rc = oc_coverage_load(&cov, o->coverage, o->n_coverage, &arena, st);
        if (rc != OC_OK)
            goto done;
        oc_coverage_mark_matches(&cov, &graph);
        have_cov = 1;
    }

    if (o->cache_path) {
        oc_status probe;
        oc_status_clear(&probe);
        rc = cache_load(&cache, o->cache_path, &arena, &probe);
        if (rc == OC_ERR_IO) {
            rc = OC_OK; /* no cache file yet: fresh start */
        } else if (rc != OC_OK) {
            oc_status_set(st, probe.code, "%s", probe.msg);
            goto done;
        }
        have_cache = 1;
    }

    /* Collect mutants per non-test analyzed file, deterministic order. */
    oc_pack_set packs;
    oc_packs_init(&packs, &arena);
    if ((rc = oc_packs_load_dir(&packs, o->packs_dir, st)) != OC_OK)
        goto done_collect;
    for (size_t i = 0; i < files.n; i++) {
        const oc_pack *pack = oc_packs_match_path(&packs, files.items[i]);
        if (!pack || is_test_path(files.items[i]))
            continue;
        char *data = NULL;
        size_t len = 0;
        if ((rc = oc_read_file(files.items[i], &data, &len, st)) != OC_OK)
            goto done_collect;
        /* SPRINT-21: the C/C++ header routing -- one routing decision per
         * file everywhere (the scan's grammar choice is authoritative). */
        pack = oc_packs_route_source(&packs, files.items[i], data, len,
                                     (const char *const *)files.items, files.n, &arena, NULL);
        if (!pack) {
            free(data);
            continue;
        }
        oc_code crc =
            oc_mutate_collect(pack->grammar, files.items[i], data, len, &arena, &rep->mutants, st);
        free(data);
        if (crc != OC_OK) {
            rc = crc;
            goto done_collect;
        }
        rep->files_mutated++;
    }
    /* Collection order = byte offset asc, op id asc, end asc. */
    if (rep->mutants.n > 1)
        qsort(rep->mutants.items, rep->mutants.n, sizeof(oc_mutant), mutant_cmp);
    rep->total = rep->mutants.n;

    /* Pre-flight: the suite must PASS on the unmutated tree. A suite
     * that fails clean would kill every mutant meaninglessly -- fail
     * loud instead of reporting a fake 100% kill ratio. */
    {
        oc_mut_status clean = run_test(o->test_cmd, timeout);
        if (clean != OC_MUT_SURVIVED) {
            oc_status_set(st, OC_ERR_USAGE,
                          "test_cmd does not pass on the unmutated tree "
                          "(exit nonzero or timeout); mutation results would "
                          "be meaningless -- fix the suite first");
            rc = OC_ERR_USAGE;
            goto done_collect;
        }
    }

    /* Execute serially in collection order (single-core budget). */
    for (size_t i = 0; i < rep->mutants.n; i++) {
        oc_mutant *m = &rep->mutants.items[i];
        if (rep->total > o->max_mutants && o->max_mutants > 0 && i >= o->max_mutants) {
            m->status = OC_MUT_SKIPPED_BUDGET;
            continue;
        }
        if (have_cov && !line_is_reachable(&graph, &cov, m->file, m->line)) {
            m->status = OC_MUT_SKIPPED_COV;
            continue;
        }
        char key[96];
        snprintf(key, sizeof(key), "%016llx|%s|%zu:%zu|%s", (unsigned long long)m->content_hash,
                 m->op_id, m->byte_start, m->byte_end, m->replacement);
        if (have_cache) {
            const char *hit = cache_map_get(&cache, key);
            if (hit) {
                m->status = string_to_status(hit);
                if (m->status == OC_MUT_KILLED || m->status == OC_MUT_SURVIVED ||
                    m->status == OC_MUT_TIMEOUT) {
                    rep->cache_hits++;
                    if (m->status == OC_MUT_KILLED)
                        rep->killed++;
                    else if (m->status == OC_MUT_SURVIVED)
                        rep->survived++;
                    else
                        rep->timeouts++;
                    continue;
                }
            }
        }

        /* Read the ORIGINAL bytes from disk (the collection copies are
         * in the arena; the file may differ -- re-verify by content). */
        char *orig = NULL;
        size_t orig_len = 0;
        if ((rc = oc_read_file(m->file, &orig, &orig_len, st)) != OC_OK)
            goto done_collect;
        if (m->byte_end > orig_len) {
            oc_status_set(st, OC_ERR_INTERNAL, "mutant range out of file bounds: %s", m->file);
            free(orig);
            rc = OC_ERR_INTERNAL;
            goto done_collect;
        }
        /* splice the mutant in */
        size_t new_len = orig_len - (m->byte_end - m->byte_start) + strlen(m->replacement);
        char *mutated = (char *)malloc(new_len);
        if (!mutated) {
            free(orig);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory splicing mutant");
            rc = OC_ERR_ALLOC;
            goto done_collect;
        }
        memcpy(mutated, orig, m->byte_start);
        memcpy(mutated + m->byte_start, m->replacement, strlen(m->replacement));
        memcpy(mutated + m->byte_start + strlen(m->replacement), orig + m->byte_end,
               orig_len - m->byte_end);
        FILE *f = fopen(m->file, "wb");
        int write_ok = f != NULL;
        if (f) {
            write_ok = fwrite(mutated, 1, new_len, f) == new_len;
            fclose(f);
        }
        free(mutated);
        if (!write_ok) {
            /* restore before failing */
            f = fopen(m->file, "wb");
            if (f) {
                fwrite(orig, 1, orig_len, f);
                fclose(f);
            }
            free(orig);
            oc_status_set(st, OC_ERR_IO, "cannot write mutated file '%s'", m->file);
            rc = OC_ERR_IO;
            goto done_collect;
        }
        rep->executed++;
        oc_mut_status verdict = run_test(o->test_cmd, timeout);
        /* restore the original bytes unconditionally */
        f = fopen(m->file, "wb");
        int restore_ok = f != NULL;
        if (f) {
            restore_ok = fwrite(orig, 1, orig_len, f) == orig_len;
            fclose(f);
        }
        free(orig);
        if (!restore_ok) {
            oc_status_set(st, OC_ERR_IO, "cannot restore mutated file '%s'", m->file);
            rc = OC_ERR_IO;
            goto done_collect;
        }
        m->status = verdict;
        if (verdict == OC_MUT_KILLED)
            rep->killed++;
        else if (verdict == OC_MUT_SURVIVED)
            rep->survived++;
        else if (verdict == OC_MUT_TIMEOUT)
            rep->timeouts++;
        if (have_cache)
            cache_map_put(&cache, key, oc_mut_status_str(verdict));
    }

done_collect:
    oc_packs_destroy(&packs);
    if (rc == OC_OK && o->cache_path)
        cache_save(&cache, o->cache_path);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    rep->wall_seconds = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;

done:
    cache_map_free(&cache);
    oc_coverage_destroy(&cov);
    oc_paths_destroy(&files);
    oc_graph_destroy(&graph);
    if (rc == OC_OK) {
        /* report (deterministic: mutants in collection order). Printed
         * BEFORE the arena teardown: the mutant records point into the
         * arena (ASan caught the use-after-free when the printer ran
         * after destroy). */
        fprintf(out, "{\"schema\": \"omnicheck.mutation_report/v0\", "
                     "\"test_cmd\": ");
        mut_json_string(out, o->test_cmd);
        fprintf(out,
                ", \"timeout_s\": %ld, \"files_mutated\": %zu, \"mutants_total\": %zu, "
                "\"executed\": %zu, \"cache_hits\": %zu, \"killed\": %zu, \"survived\": %zu, "
                "\"timeout\": %zu, \"wall_seconds\": %.2f, \"mutants\": [",
                timeout, rep->files_mutated, rep->total, rep->executed, rep->cache_hits,
                rep->killed, rep->survived, rep->timeouts, rep->wall_seconds);
        for (size_t i = 0; i < rep->mutants.n; i++) {
            const oc_mutant *m = &rep->mutants.items[i];
            fprintf(out, "%s\n    {\"file\": ", i ? "," : "");
            mut_json_string(out, m->file);
            fprintf(out, ", \"line\": %zu, \"op\": \"%s\", \"original\": ", m->line, m->op_id);
            mut_json_string(out, m->original);
            fprintf(out, ", \"replacement\": ");
            mut_json_string(out, m->replacement);
            fprintf(out, ", \"status\": \"%s\"}", oc_mut_status_str(m->status));
        }
        fprintf(out, "\n  ]\n}\n");
        oc_status_clear(st);
    }
    oc_arena_destroy(&arena);
    return rc;
}

#endif /* OC_WITH_TREE_SITTER */
