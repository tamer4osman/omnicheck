#include "oc/coverage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oc/graph.h"
#include "oc/json.h"

/* H-3 (P-3): the Go coverprofile loader derives a malloc size from the
 * record's end line. An unbounded `el` let 47 bytes of input ask for a
 * 4 GB allocation (measured: 3,615 MiB peak RSS, SIGKILL -- evidence
 * T5). 10,000,000 is a generous ceiling (no real coverage report
 * touches files anywhere near that tall); anything beyond it is a
 * hostile record and fails the load LOUDLY with OC_ERR_USAGE, matching
 * the OpenCover loader's existing sanity bound in this same file. */
#define OC_COV_MAX_LINE 10000000UL

/* ---------------------------------------------------------------- sets */

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int cmp_cov_file(const void *a, const void *b) {
    return strcmp(((const oc_cov_file *)a)->path, ((const oc_cov_file *)b)->path);
}

/* Sorted, deduped copy of (lines, n) into an exact-size heap array.
 * Returns NULL only when allocation fails (n > 0). */
static uint32_t *set_build(const uint32_t *lines, size_t n, size_t *out_n) {
    *out_n = 0;
    if (n == 0)
        return NULL;
    uint32_t *buf = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!buf)
        return NULL;
    memcpy(buf, lines, n * sizeof(uint32_t));
    qsort(buf, n, sizeof(uint32_t), cmp_u32);
    size_t w = 1;
    for (size_t i = 1; i < n; i++) {
        if (buf[i] != buf[w - 1])
            buf[w++] = buf[i];
    }
    *out_n = w;
    return buf;
}

/* In-place union of line arrays into dst (dst grows on demand).
 * Returns 0 on OOM. */
static int set_union(uint32_t **dst, size_t *n_dst, size_t *cap_dst, const uint32_t *add,
                     size_t n_add) {
    if (n_add == 0)
        return 1;
    size_t out = *n_dst + n_add;
    if (out > *cap_dst) {
        size_t cap = *cap_dst ? *cap_dst : 16;
        while (cap < out)
            cap *= 2;
        uint32_t *grown = (uint32_t *)realloc(*dst, cap * sizeof(uint32_t));
        if (!grown)
            return 0;
        *dst = grown;
        *cap_dst = cap;
    }
    memcpy(*dst + *n_dst, add, n_add * sizeof(uint32_t));
    *n_dst = out;
    qsort(*dst, *n_dst, sizeof(uint32_t), cmp_u32);
    size_t w = 1;
    for (size_t i = 1; i < *n_dst; i++) {
        if ((*dst)[i] != (*dst)[w - 1])
            (*dst)[w++] = (*dst)[i];
    }
    *n_dst = w;
    return 1;
}

/* ----------------------------------------------------------- entries */

static oc_cov_file *cov_entry(oc_coverage *cov, const char *path) {
    for (size_t i = 0; i < cov->n_files; i++) {
        if (strcmp(cov->files[i].path, path) == 0)
            return &cov->files[i];
    }
    if (cov->n_files == cov->cap_files) {
        size_t cap = cov->cap_files ? cov->cap_files * 2 : 16;
        oc_cov_file *grown = (oc_cov_file *)realloc(cov->files, cap * sizeof(*grown));
        if (!grown)
            return NULL;
        cov->files = grown;
        cov->cap_files = cap;
    }
    oc_cov_file *e = &cov->files[cov->n_files++];
    memset(e, 0, sizeof(*e));
    e->path = strdup(path);
    if (!e->path) {
        cov->n_files--;
        return NULL;
    }
    return e;
}

void oc_coverage_init(oc_coverage *cov) {
    memset(cov, 0, sizeof(*cov));
}

void oc_coverage_destroy(oc_coverage *cov) {
    if (!cov)
        return;
    for (size_t i = 0; i < cov->n_files; i++) {
        free(cov->files[i].path);
        free(cov->files[i].anchor);
        free(cov->files[i].covered);
    }
    free(cov->files);
    memset(cov, 0, sizeof(*cov));
}

/* ------------------------------------------------------- Istanbul ---- */

/* All statement anchor lines for one Istanbul entry: statementMap[id].
 * start.line for every statement id. */
static int istanbul_anchor_lines(const oc_json *entry, uint32_t **out, size_t *out_n) {
    const oc_json *stmt = oc_json_get(entry, "statementMap");
    if (!stmt || stmt->type != OC_JSON_OBJ) {
        *out = NULL;
        *out_n = 0;
        return 1; /* entry without statements: nothing anchored */
    }
    uint32_t *lines = (uint32_t *)malloc((stmt->n_fields ? stmt->n_fields : 1) * sizeof(uint32_t));
    if (!lines)
        return 0;
    size_t w = 0;
    for (size_t i = 0; i < stmt->n_fields; i++) {
        const oc_json *start = oc_json_get(stmt->vals[i], "start");
        if (!start)
            continue;
        double ln = oc_json_as_num(oc_json_get(start, "line"), 0.0);
        if (ln >= 1)
            lines[w++] = (uint32_t)ln;
    }
    size_t n = 0;
    uint32_t *set = set_build(lines, w, &n);
    free(lines);
    if (w > 0 && !set)
        return 0;
    *out = set;
    *out_n = n;
    return 1;
}

/* Covered-line set for one Istanbul entry: statement lines whose hit
 * count s[id] > 0. */
static int istanbul_covered_lines(const oc_json *entry, uint32_t **out, size_t *out_n) {
    const oc_json *stmt = oc_json_get(entry, "statementMap");
    const oc_json *s = oc_json_get(entry, "s");
    if (!stmt || stmt->type != OC_JSON_OBJ || !s || s->type != OC_JSON_OBJ) {
        *out = NULL;
        *out_n = 0;
        return 1;
    }
    uint32_t *lines = (uint32_t *)malloc((s->n_fields ? s->n_fields : 1) * sizeof(uint32_t));
    if (!lines)
        return 0;
    size_t w = 0;
    for (size_t i = 0; i < s->n_fields; i++) {
        if (oc_json_as_num(s->vals[i], 0.0) <= 0)
            continue;
        const oc_json *sm = oc_json_get(stmt, s->keys[i]);
        if (!sm)
            continue;
        double ln = oc_json_as_num(oc_json_get(oc_json_get(sm, "start"), "line"), 0.0);
        if (ln >= 1)
            lines[w++] = (uint32_t)ln;
    }
    size_t n = 0;
    uint32_t *set = set_build(lines, w, &n);
    free(lines);
    if (w > 0 && !set)
        return 0;
    *out = set;
    *out_n = n;
    return 1;
}

static oc_code load_istanbul(oc_coverage *cov, const oc_json *root, oc_status *st) {
    for (size_t i = 0; i < root->n_fields; i++) {
        const oc_json *entry = root->vals[i];
        if (entry->type != OC_JSON_OBJ)
            continue;
        uint32_t *al = NULL, *cl = NULL;
        size_t na = 0, nc = 0;
        if (!istanbul_anchor_lines(entry, &al, &na) || !istanbul_covered_lines(entry, &cl, &nc)) {
            free(al);
            free(cl);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory composing Istanbul coverage");
            return OC_ERR_ALLOC;
        }
        const char *path = oc_json_as_str(oc_json_get(entry, "path"), root->keys[i]);
        oc_cov_file *e = cov_entry(cov, path);
        if (!e) {
            free(al);
            free(cl);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
            return OC_ERR_ALLOC;
        }
        int ok = set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, al, na) &&
                 set_union(&e->covered, &e->n_covered, &e->cap_covered, cl, nc);
        free(al);
        free(cl);
        if (!ok) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning coverage lines");
            return OC_ERR_ALLOC;
        }
        cov->files_loaded++;
    }
    return OC_OK;
}

/* ------------------------------------------------------ coverage.py --- */

static oc_code load_pycov(oc_coverage *cov, const oc_json *root, oc_status *st) {
    const oc_json *files = oc_json_get(root, "files");
    if (!files || files->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "coverage.py report has no 'files' object");
        return OC_ERR_USAGE;
    }
    for (size_t i = 0; i < files->n_fields; i++) {
        const oc_json *f = files->vals[i];
        if (f->type != OC_JSON_OBJ)
            continue;
        const oc_json *exec = oc_json_get(f, "executed_lines");
        const oc_json *missing = oc_json_get(f, "missing_lines");
        size_t n_exec = (exec && exec->type == OC_JSON_ARR) ? exec->n_items : 0;
        size_t n_miss = (missing && missing->type == OC_JSON_ARR) ? missing->n_items : 0;
        if (n_exec + n_miss == 0) {
            cov->files_loaded++; /* file with no executable lines */
            continue;
        }
        uint32_t *lines = (uint32_t *)malloc((n_exec + n_miss) * sizeof(uint32_t));
        uint32_t *exec_lines = (uint32_t *)malloc((n_exec ? n_exec : 1) * sizeof(uint32_t));
        if (!lines || !exec_lines) {
            free(lines);
            free(exec_lines);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory composing coverage.py lines");
            return OC_ERR_ALLOC;
        }
        size_t w = 0, we = 0;
        for (size_t j = 0; j < n_exec; j++) {
            double v = oc_json_as_num(exec->items[j], 0.0);
            if (v >= 1) {
                lines[w++] = (uint32_t)v;
                exec_lines[we++] = (uint32_t)v;
            }
        }
        for (size_t j = 0; j < n_miss; j++) {
            double v = oc_json_as_num(missing->items[j], 0.0);
            if (v >= 1)
                lines[w++] = (uint32_t)v;
        }
        size_t all_n = 0, cov_n = 0;
        uint32_t *all = set_build(lines, w, &all_n);
        uint32_t *covl = set_build(exec_lines, we, &cov_n);
        free(lines);
        free(exec_lines);
        if ((w > 0 && !all) || (we > 0 && !covl)) {
            free(all);
            free(covl);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory composing coverage.py anchors");
            return OC_ERR_ALLOC;
        }
        oc_cov_file *e = cov_entry(cov, files->keys[i]);
        if (!e) {
            free(all);
            free(covl);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
            return OC_ERR_ALLOC;
        }
        int ok = set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, all, all_n) &&
                 set_union(&e->covered, &e->n_covered, &e->cap_covered, covl, cov_n);
        free(all);
        free(covl);
        if (!ok) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning coverage.py lines");
            return OC_ERR_ALLOC;
        }
        cov->files_loaded++;
    }
    return OC_OK;
}

/* ---------------------------------------------------------- V8 (JS) ---- */

/* V8 adapter (WBS 4.3): NODE_V8_COVERAGE-style JSON
 * {"result":[{"url":"file:///abs/path.js","functions":[{"functionName":
 * ..., "isBlockCoverage":true,"ranges":[{"startOffset":N,"endOffset":M,
 * "count":K}]}]}]}.
 *
 * Line-anchor composition contract (same as Istanbul/coverage.py): a
 * line is an ANCHOR when any function range contains its start byte and
 * COVERED when the deepest (smallest) containing range has count > 0 --
 * an uncovered inner block shadows a covered enclosing function, which
 * is exactly the partial-coverage information V8 block coverage
 * carries. The file is read from disk once to map byte offsets to lines
 * (the JSON has no line numbers of its own); an unreadable url is a
 * hard error, never a silent skip. */

static char *v8_url_to_path(oc_arena *a, const char *url) {
    const char *p = url;
    if (strncmp(p, "file://", 7) == 0)
        p += 7;
    /* percent-decode in place; arena owns the result */
    char *out = oc_arena_strdup(a, p);
    if (!out)
        return NULL;
    for (size_t i = 0, w = 0; out[i]; i++) {
        if (out[i] == '%' && out[i + 1] && out[i + 2]) {
            char hex[3] = {out[i + 1], out[i + 2], 0};
            out[w++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            out[w++] = out[i];
        }
    }
    return out;
}

static oc_code load_v8(oc_coverage *cov, const oc_json *root, oc_arena *a, oc_status *st) {
    const oc_json *result = oc_json_get(root, "result");
    if (!result || result->type != OC_JSON_ARR) {
        oc_status_set(st, OC_ERR_USAGE, "V8 coverage JSON lacks a \"result\" array");
        return OC_ERR_USAGE;
    }
    for (size_t i = 0; i < oc_json_size(result); i++) {
        const oc_json *entry = result->items[i];
        if (entry->type != OC_JSON_OBJ)
            continue;
        const char *url = oc_json_as_str(oc_json_get(entry, "url"), "");
        char *path = v8_url_to_path(a, url);
        if (!path) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory decoding coverage url");
            return OC_ERR_ALLOC;
        }
        char *data = NULL;
        size_t dlen = 0;
        oc_code rc = oc_read_file(path, &data, &dlen, st);
        if (rc != OC_OK) {
            oc_status_set(st, OC_ERR_IO,
                          "V8 coverage url '%s' cannot be read for "
                          "offset-to-line mapping",
                          path);
            return OC_ERR_IO;
        }
        /* line starts (1-based line numbers, offset 0-based bytes) */
        size_t cap_lines = 64, n_lines = 0;
        size_t *starts = (size_t *)malloc(cap_lines * sizeof(size_t));
        if (!starts) {
            free(data);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory mapping coverage lines");
            return OC_ERR_ALLOC;
        }
        starts[n_lines++] = 0;
        for (size_t b = 0; b < dlen; b++) {
            if (data[b] == '\n') {
                if (n_lines == cap_lines) {
                    cap_lines *= 2;
                    size_t *g = (size_t *)realloc(starts, cap_lines * sizeof(size_t));
                    if (!g) {
                        free(starts);
                        free(data);
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory mapping lines");
                        return OC_ERR_ALLOC;
                    }
                    starts = g;
                }
                starts[n_lines++] = b + 1;
            }
        }
        /* flatten all function ranges */
        typedef struct {
            size_t start, end;
            long count;
        } v8range;
        const oc_json *functions = oc_json_get(entry, "functions");
        size_t n_ranges = 0;
        for (size_t f = 0; functions && f < oc_json_size(functions); f++) {
            const oc_json *ranges = oc_json_get(functions->items[f], "ranges");
            if (ranges && ranges->type == OC_JSON_ARR)
                n_ranges += oc_json_size(ranges);
        }
        v8range *rng = NULL;
        if (n_ranges > 0) {
            rng = (v8range *)malloc(n_ranges * sizeof(*rng));
            if (!rng) {
                free(starts);
                free(data);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing V8 ranges");
                return OC_ERR_ALLOC;
            }
            size_t w = 0;
            for (size_t f = 0; f < oc_json_size(functions); f++) {
                const oc_json *ranges = oc_json_get(functions->items[f], "ranges");
                if (!ranges || ranges->type != OC_JSON_ARR)
                    continue;
                for (size_t r = 0; r < oc_json_size(ranges); r++) {
                    const oc_json *rj = ranges->items[r];
                    long count = (long)oc_json_as_num(oc_json_get(rj, "count"), 0.0);
                    double so = oc_json_as_num(oc_json_get(rj, "startOffset"), -1.0);
                    double eo = oc_json_as_num(oc_json_get(rj, "endOffset"), -1.0);
                    if (so < 0 || eo < so || (size_t)eo > dlen)
                        continue; /* malformed range: skipped, honestly */
                    rng[w].start = (size_t)so;
                    rng[w].end = (size_t)eo;
                    rng[w].count = count;
                    w++;
                }
            }
            n_ranges = w;
        }
        uint32_t *anchor = (uint32_t *)malloc(n_lines * sizeof(uint32_t));
        uint32_t *covered = (uint32_t *)malloc(n_lines * sizeof(uint32_t));
        if (!anchor || !covered) {
            free(anchor);
            free(covered);
            free(rng);
            free(starts);
            free(data);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory composing V8 anchors");
            return OC_ERR_ALLOC;
        }
        size_t n_anchor = 0, n_covered = 0;
        for (size_t l = 0; l < n_lines; l++) {
            size_t b = starts[l];
            /* deepest range containing the line start */
            long best = -1;
            size_t best_len = 0;
            for (size_t r = 0; r < n_ranges; r++) {
                if (rng[r].start <= b && b < rng[r].end) {
                    size_t len = rng[r].end - rng[r].start;
                    if (best < 0 || len < best_len) {
                        best = rng[r].count;
                        best_len = len;
                    }
                }
            }
            if (best < 0)
                continue; /* no function range claims this line */
            anchor[n_anchor++] = (uint32_t)(l + 1);
            if (best > 0)
                covered[n_covered++] = (uint32_t)(l + 1);
        }
        free(rng);
        free(starts);
        free(data);
        oc_cov_file *e = cov_entry(cov, path);
        if (!e || !set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, anchor, n_anchor) ||
            !set_union(&e->covered, &e->n_covered, &e->cap_covered, covered, n_covered)) {
            free(anchor);
            free(covered);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory composing V8 anchors");
            return OC_ERR_ALLOC;
        }
        free(anchor);
        free(covered);
        cov->files_loaded++;
    }
    return OC_OK;
}

/* ------------------------------------------------------------ JaCoCo --
 *
 * JaCoCo XML adapter (SPRINT-11, docs/LANGUAGES.md §Java.6 -- the
 * pre-committed Java coverage adapter). JaCoCo's XML report nests
 * <package name="a/b"> > <sourcefile name="Foo.java"> > <method ...>
 * elements, each carrying <counter type="LINE" missed="M" covered="C"/>.
 *
 * Method-level mapping contract (pre-committed in the language
 * contract): a method's `line` attribute is the ANCHOR line and the
 * method is COVERED when its LINE counter's covered attribute is > 0;
 * the standard path rule (oc_coverage_lookup: exact or unique
 * "/"-boundary suffix match, then anchors inside the OC definition
 * span) maps those anchors onto Java definition extents exactly like
 * the other adapters. The report paths are package-relative
 * ("a/b/Foo.java"), so the suffix rule bridges source-root prefixes.
 * This is NOT an XML parser: a bounded scanner reacts only to the four
 * element names it understands (report/package/sourcefile/method/counter
 * + their attributes), skips everything else honestly, and FAILS LOUD
 * when the file is not XML or lacks a <report> root -- a truncated or
 * foreign file is never silently half-parsed. */

typedef struct {
    const char *pkg;   /* current package name attr (arena), NULL outside */
    const char *sfile; /* current sourcefile name attr (arena), NULL outside */
    size_t method_line;
    long method_covered;
    int in_method;
    oc_cov_file *entry;
} jacoco_state;

/* Extract an attribute value from a tag body between tag_end points. */
static const char *jacoco_attr(const char *tag, const char *tag_end, const char *attr,
                               oc_arena *a) {
    size_t alen = strlen(attr);
    for (const char *p = tag; p < tag_end; p++) {
        if (*p != attr[0] || (size_t)(tag_end - p) <= alen || memcmp(p, attr, alen) != 0)
            continue;
        const char *q = p + alen;
        while (q < tag_end && (*q == ' ' || *q == '\t'))
            q++;
        if (q >= tag_end || *q != '=')
            continue;
        q++;
        while (q < tag_end && (*q == ' ' || *q == '\t'))
            q++;
        if (q >= tag_end || (*q != '"' && *q != '\''))
            continue;
        char quote = *q++;
        const char *end = memchr(q, quote, (size_t)(tag_end - q));
        if (!end)
            return NULL;
        return oc_arena_strndup(a, q, (size_t)(end - q));
    }
    return NULL;
}

static oc_code load_jacoco(oc_coverage *cov, const char *data, size_t len, oc_arena *a,
                           oc_status *st) {
    (void)len;
    jacoco_state js;
    memset(&js, 0, sizeof(js));
    int saw_report = 0;

    const char *p = data;
    while (p && *p) {
        const char *lt = strchr(p, '<');
        if (!lt)
            break;
        const char *c = lt + 1;
        if (*c == '!') { /* comment/DOCTYPE: skip whole construct */
            if (c[1] == '-' && c[2] == '-') {
                const char *end = strstr(c + 3, "-->");
                if (!end)
                    break;
                p = end + 3;
            } else {
                const char *end = strchr(c, '>');
                if (!end)
                    break;
                p = end + 1;
            }
            continue;
        }
        if (*c == '?') {
            const char *end = strchr(c, '>');
            if (!end)
                break;
            p = end + 1;
            continue;
        }
        int closing = (*c == '/');
        if (closing)
            c++;
        const char *nstart = c;
        while (*c && *c != '>' && *c != ' ' && *c != '\t' && *c != '\r' && *c != '\n' && *c != '/')
            c++;
        size_t nlen = (size_t)(c - nstart);
        const char *gt = strchr(c, '>');
        if (!gt)
            break;
        int selfclosing = (gt > c && gt[-1] == '/');
        p = gt + 1;
        if (nlen == 0)
            continue;

        if (!closing && nlen == 6 && memcmp(nstart, "report", 6) == 0) {
            saw_report = 1;
            continue;
        }
        if (!saw_report)
            continue; /* everything before the report root is ignored */

        if (!closing && nlen == 7 && memcmp(nstart, "package", 7) == 0 && !selfclosing) {
            js.pkg = jacoco_attr(nstart, gt, "name", a);
            js.sfile = NULL;
        } else if (closing && nlen == 7 && memcmp(nstart, "package", 7) == 0) {
            js.pkg = NULL;
            js.sfile = NULL;
        } else if (!closing && nlen == 10 && memcmp(nstart, "sourcefile", 10) == 0 &&
                   !selfclosing) {
            js.sfile = jacoco_attr(nstart, gt, "name", a);
            js.entry = NULL;
            if (js.pkg && js.sfile) {
                char joined[1200];
                if (snprintf(joined, sizeof(joined), "%s/%s", js.pkg, js.sfile) <
                    (int)sizeof(joined)) {
                    js.entry = cov_entry(cov, joined);
                    if (!js.entry) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
                        return OC_ERR_ALLOC;
                    }
                }
            }
            if (js.entry)
                cov->files_loaded++;
        } else if (closing && nlen == 10 && memcmp(nstart, "sourcefile", 10) == 0) {
            js.sfile = NULL;
            js.entry = NULL;
        } else if (js.entry && !closing && nlen == 6 && memcmp(nstart, "method", 6) == 0 &&
                   !selfclosing) {
            /* method anchor: the `line` attribute; coverage decided by
             * the method's LINE counter below (or 0 when absent) */
            js.in_method = 1;
            js.method_covered = 0;
            double ln = 0.0;
            const char *lattr = jacoco_attr(nstart, gt, "line", a);
            if (lattr)
                ln = strtod(lattr, NULL);
            js.method_line = ln >= 1 ? (size_t)ln : 0;
        } else if (js.entry && closing && nlen == 6 && memcmp(nstart, "method", 6) == 0) {
            if (js.in_method && js.method_line >= 1) {
                uint32_t anchor[1] = {(uint32_t)js.method_line};
                uint32_t covered[1] = {(uint32_t)js.method_line};
                if (!set_union(&js.entry->anchor, &js.entry->n_anchor, &js.entry->cap_anchor,
                               anchor, 1) ||
                    (js.method_covered > 0 && !set_union(&js.entry->covered, &js.entry->n_covered,
                                                         &js.entry->cap_covered, covered, 1))) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning JaCoCo lines");
                    return OC_ERR_ALLOC;
                }
            }
            js.in_method = 0;
        } else if (js.in_method && nlen == 7 && memcmp(nstart, "counter", 7) == 0) {
            /* <counter type="LINE" missed=".." covered=".."/>: decisive
             * only for type="LINE"; everything else is skipped */
            const char *ctype = jacoco_attr(nstart, gt, "type", a);
            if (ctype && strcmp(ctype, "LINE") == 0) {
                const char *cattr = jacoco_attr(nstart, gt, "covered", a);
                js.method_covered = cattr ? strtol(cattr, NULL, 10) : 0;
            }
        }
    }
    if (!saw_report) {
        oc_status_set(st, OC_ERR_USAGE, "coverage file is XML but carries no <report> root");
        return OC_ERR_USAGE;
    }
    /* Deterministic order for suffix matching + report meta. (M-10/T16
     * hygiene: guard qsort against a NULL base on zero files -- UB.) */
    if (cov->n_files > 0)
        qsort(cov->files, cov->n_files, sizeof(oc_cov_file), cmp_cov_file);
    return OC_OK;
}

/* ------------------------------------------------------ go cover ---- */

/* Go coverprofile adapter (SPRINT-11, docs/LANGUAGES.md §Go.6): the text
 * format of `go test -coverprofile` --
 *
 *   mode: set
 *   pkg/dir/name.go:11.32,13.10 2 1
 *
 * one record per basic block: file:startLine.startCol,endLine.endCol
 * numStmts count. `mode: set`, `count` and `atomic` carry the same
 * information for our purpose: count > 0 is covered. Anchor semantics
 * (contracted, §Go.6): the inclusive line range of the block is
 * anchored and covered when count > 0 -- a block IS the statement unit
 * Go's profiler measures, so its whole line range is the honest line
 * projection. The file path is taken verbatim as written; matching
 * against analyzed paths is the standard exact-then-unique-suffix rule
 * (oc_coverage_lookup). A malformed record fails loud (never compose
 * silently), same as every other coverage format. */
/* Frees the coverprofile distinct-path tracking set (LSan discipline:
 * every strdup in a loader has exactly one cleanup path). */
static void free_seen_set(char **seen, size_t n_seen) {
    for (size_t k = 0; k < n_seen; k++)
        free(seen[k]);
    free(seen);
}

static oc_code load_go_coverprofile(oc_coverage *cov, const char *data, const char *src_name,
                                    oc_status *st) {
    const char *p = data;
    int seen_mode = 0;
    size_t lineno = 0;
    /* distinct-path set for the honest files_loaded meta counter (the
     * other formats count one entry per report object; the coverprofile
     * has one record per BLOCK) */
    char **seen = NULL;
    size_t n_seen = 0, cap_seen = 0;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        if (linelen > 0 && p[linelen - 1] == '\r')
            linelen--;
        lineno++;
        if (linelen == 0) {
            p = eol ? eol + 1 : NULL;
            continue;
        }
        if (!seen_mode && linelen >= 5 && memcmp(p, "mode:", 5) == 0) {
            seen_mode = 1;
            p = eol ? eol + 1 : NULL;
            continue;
        }
        /* "path:startLine.startCol,endLine.endCol numStmts count" */
        const char *colon = memchr(p, ':', linelen);
        const char *space = memchr(p, ' ', linelen);
        if (!colon || !space || space < colon) {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu is not a block record",
                          src_name, lineno);
            return OC_ERR_USAGE;
        }
        char path[1024];
        size_t plen = (size_t)(colon - p);
        if (plen == 0 || plen >= sizeof(path)) {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: bad file path",
                          src_name, lineno);
            return OC_ERR_USAGE;
        }
        memcpy(path, p, plen);
        path[plen] = '\0';
        /* position = startLine.startCol,endLine.endCol */
        const char *pos = colon + 1;
        size_t pos_len = (size_t)(space - pos);
        char posbuf[96];
        if (pos_len == 0 || pos_len >= sizeof(posbuf)) {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: bad position", src_name,
                          lineno);
            return OC_ERR_USAGE;
        }
        memcpy(posbuf, pos, pos_len);
        posbuf[pos_len] = '\0';
        unsigned long sl = 0, el = 0;
        char *endp = NULL;
        sl = strtoul(posbuf, &endp, 10);
        if (endp == posbuf || *endp != '.') {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: bad start line",
                          src_name, lineno);
            return OC_ERR_USAGE;
        }
        const char *comma = strchr(endp, ',');
        if (!comma) {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: missing ','", src_name,
                          lineno);
            return OC_ERR_USAGE;
        }
        el = strtoul(comma + 1, &endp, 10);
        if (endp == comma + 1 || *endp != '.') {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: bad end line", src_name,
                          lineno);
            return OC_ERR_USAGE;
        }
        /* count is the third field (after numStmts) */
        char cntbuf[64];
        size_t clen = linelen - (size_t)(space - p);
        const char *cs = space + 1;
        /* fields: "numStmts count" -- skip to the last field */
        const char *c2 = cs;
        size_t c2len = clen;
        const char *s2 = memchr(cs, ' ', c2len);
        if (!s2) {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: missing count",
                          src_name, lineno);
            return OC_ERR_USAGE;
        }
        c2 = s2 + 1;
        c2len = linelen - (size_t)(c2 - p);
        if (c2len == 0 || c2len >= sizeof(cntbuf)) {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: bad count", src_name,
                          lineno);
            return OC_ERR_USAGE;
        }
        memcpy(cntbuf, c2, c2len);
        cntbuf[c2len] = '\0';
        char *cend = NULL;
        long count = strtol(cntbuf, &cend, 10);
        if (cend == cntbuf || *cend != '\0') {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: bad count", src_name,
                          lineno);
            return OC_ERR_USAGE;
        }
        if (sl < 1 || el < sl) {
            oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' line %zu: bad line range",
                          src_name, lineno);
            return OC_ERR_USAGE;
        }
        if (el > OC_COV_MAX_LINE || sl > OC_COV_MAX_LINE) {
            oc_status_set(st, OC_ERR_USAGE,
                          "go coverprofile '%s' line %zu: line range %lu-%lu exceeds the "
                          "analysis budget (%lu lines per record)",
                          src_name, lineno, sl, el, OC_COV_MAX_LINE);
            return OC_ERR_USAGE;
        }
        oc_cov_file *e = cov_entry(cov, path);
        if (!e) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
            free_seen_set(seen, n_seen);
            return OC_ERR_ALLOC;
        }
        int fresh = 1;
        for (size_t k = 0; k < n_seen; k++) {
            if (strcmp(seen[k], path) == 0) {
                fresh = 0;
                break;
            }
        }
        if (fresh) {
            if (n_seen == cap_seen) {
                size_t ncap = cap_seen ? cap_seen * 2 : 8;
                char **grown = (char **)realloc(seen, ncap * sizeof(*grown));
                if (!grown) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory tracking coverprofile paths");
                    free_seen_set(seen, n_seen);
                    return OC_ERR_ALLOC;
                }
                seen = grown;
                cap_seen = ncap;
            }
            seen[n_seen] = strdup(path);
            if (!seen[n_seen]) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory tracking coverprofile paths");
                free_seen_set(seen, n_seen);
                return OC_ERR_ALLOC;
            }
            n_seen++;
            cov->files_loaded++;
        }
        size_t span = (size_t)(el - sl + 1);
        uint32_t *lines = (uint32_t *)malloc(span * sizeof(uint32_t));
        if (!lines) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory composing go coverprofile lines");
            free_seen_set(seen, n_seen);
            return OC_ERR_ALLOC;
        }
        for (size_t l = 0; l < span; l++)
            lines[l] = (uint32_t)(sl + l);
        size_t all_n = 0;
        uint32_t *all = set_build(lines, span, &all_n);
        size_t cov_n = 0;
        uint32_t *covl = count > 0 ? set_build(lines, span, &cov_n) : NULL;
        free(lines);
        if (span > 0 && (!all || (count > 0 && !covl))) {
            free(all);
            free(covl);
            oc_status_set(st, OC_ERR_ALLOC, "out of memory composing go coverprofile anchors");
            free_seen_set(seen, n_seen);
            return OC_ERR_ALLOC;
        }
        int ok = set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, all, all_n);
        if (ok && covl)
            ok = set_union(&e->covered, &e->n_covered, &e->cap_covered, covl, cov_n);
        free(all);
        free(covl);
        if (!ok) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning go coverprofile lines");
            free_seen_set(seen, n_seen);
            return OC_ERR_ALLOC;
        }
        p = eol ? eol + 1 : NULL;
    }
    if (!seen_mode) {
        oc_status_set(st, OC_ERR_USAGE, "go coverprofile '%s' lacks a 'mode:' header", src_name);
        free_seen_set(seen, n_seen);
        return OC_ERR_USAGE;
    }
    free_seen_set(seen, n_seen);
    return OC_OK;
}

/* LCOV adapter (SPRINT-15, docs/LANGUAGES.md Rust section 6): the
 * lcov/geninfo text format -- the open format the Rust ecosystem's
 * coverage tools actually emit (grcov, cargo-llvm-cov --lcov,
 * cargo-tarpaulin). Structure: per-record blocks
 *
 *   TN:<test name>
 *   SF:<file path>
 *   FN:<line>,<name> / FNDA:<count>,<name>
 *   DA:<line>,<count>
 *   BRDA:<line>,<block>,<branch>,<taken>
 *   LF:<lines found> / LH:<lines hit>
 *   end_of_record
 *
 * Anchor semantics (contracted): a line is an anchor when the report
 * carries a DA record for it, covered when the hit count > 0 -- DA is
 * the only record that carries BOTH a line anchor and a hit count
 * (FN/FNDA are function-keyed, BRDA is branch-keyed with no honest
 * line-anchored hit model in v1 -- consumed and skipped, never
 * fabricated). SF paths join to analyzed paths through the standard
 * exact-then-unique-suffix rule (oc_coverage_lookup). Malformed input
 * fails LOUD (the Go coverprofile discipline): a DA/BRDA/LF/LH/
 * end_of_record before any SF:, a non-numeric DA line or count, or EOF
 * inside an open record are USAGE errors, never silently partial. */
static oc_code load_lcov(oc_coverage *cov, const char *data, const char *src_name, oc_status *st) {
    const char *p = data;
    size_t lineno = 0;
    /* one files_loaded per distinct SF record (the coverprofile
     * distinct-path discipline; LSan: every strdup has one cleanup) */
    char **seen = NULL;
    size_t n_seen = 0, cap_seen = 0;
    oc_cov_file *cur = NULL;
    oc_code rc = OC_OK;
    while (p && *p && rc == OC_OK) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        if (linelen > 0 && p[linelen - 1] == '\r')
            linelen--;
        lineno++;
        /* "end_of_record" closes the current record (no colon key); a
         * close before any SF: is a malformed report */
        if (linelen == 13 && memcmp(p, "end_of_record", 13) == 0) {
            if (!cur) {
                rc = OC_ERR_USAGE;
                oc_status_set(st, OC_ERR_USAGE,
                              "lcov report '%s' line %zu: end_of_record before any SF:", src_name,
                              lineno);
                break;
            }
            cur = NULL;
            p = eol ? eol + 1 : NULL;
            continue;
        }
        const char *colon = linelen >= 1 ? memchr(p, ':', linelen) : NULL;
        if (colon) {
            size_t klen = (size_t)(colon - p);
            const char *val = colon + 1;
            size_t vlen = linelen - klen - 1;
            if (klen == 2 && memcmp(p, "SF", 2) == 0) {
                char path[1024];
                if (vlen == 0 || vlen >= sizeof(path)) {
                    rc = OC_ERR_USAGE;
                    oc_status_set(st, OC_ERR_USAGE, "lcov report '%s' line %zu: bad SF path",
                                  src_name, lineno);
                    break;
                }
                memcpy(path, val, vlen);
                path[vlen] = '\0';
                cur = cov_entry(cov, path);
                if (!cur) {
                    rc = OC_ERR_ALLOC;
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
                    break;
                }
                int fresh = 1;
                for (size_t k = 0; k < n_seen; k++) {
                    if (strcmp(seen[k], path) == 0) {
                        fresh = 0;
                        break;
                    }
                }
                if (fresh) {
                    if (n_seen == cap_seen) {
                        size_t ncap = cap_seen ? cap_seen * 2 : 8;
                        char **grown = (char **)realloc(seen, ncap * sizeof(*grown));
                        if (!grown) {
                            rc = OC_ERR_ALLOC;
                            break;
                        }
                        seen = grown;
                        cap_seen = ncap;
                    }
                    seen[n_seen] = strdup(path);
                    if (!seen[n_seen]) {
                        rc = OC_ERR_ALLOC;
                        break;
                    }
                    n_seen++;
                    cov->files_loaded++;
                }
                p = eol ? eol + 1 : NULL;
                continue;
            }
            if (klen == 2 && memcmp(p, "DA", 2) == 0) {
                if (!cur) {
                    rc = OC_ERR_USAGE;
                    oc_status_set(st, OC_ERR_USAGE,
                                  "lcov report '%s' line %zu: DA before any SF:", src_name, lineno);
                    break;
                }
                /* DA:<line>,<count> */
                char valbuf[64];
                if (vlen == 0 || vlen >= sizeof(valbuf)) {
                    rc = OC_ERR_USAGE;
                    oc_status_set(st, OC_ERR_USAGE, "lcov report '%s' line %zu: bad DA record",
                                  src_name, lineno);
                    break;
                }
                memcpy(valbuf, val, vlen);
                valbuf[vlen] = '\0';
                char *endp = NULL;
                unsigned long ln = strtoul(valbuf, &endp, 10);
                if (endp == valbuf || *endp != ',') {
                    rc = OC_ERR_USAGE;
                    oc_status_set(st, OC_ERR_USAGE, "lcov report '%s' line %zu: bad DA line number",
                                  src_name, lineno);
                    break;
                }
                char *cnt = endp + 1;
                long count = strtol(cnt, &endp, 10);
                if (endp == cnt || (*endp != '\0' && *endp != ',')) {
                    rc = OC_ERR_USAGE;
                    oc_status_set(st, OC_ERR_USAGE, "lcov report '%s' line %zu: bad DA count",
                                  src_name, lineno);
                    break;
                }
                if (ln < 1) {
                    rc = OC_ERR_USAGE;
                    oc_status_set(st, OC_ERR_USAGE, "lcov report '%s' line %zu: bad DA line range",
                                  src_name, lineno);
                    break;
                }
                uint32_t line32 = (uint32_t)ln;
                if (!set_union(&cur->anchor, &cur->n_anchor, &cur->cap_anchor, &line32, 1)) {
                    rc = OC_ERR_ALLOC;
                    break;
                }
                if (count > 0 &&
                    !set_union(&cur->covered, &cur->n_covered, &cur->cap_covered, &line32, 1)) {
                    rc = OC_ERR_ALLOC;
                    break;
                }
                p = eol ? eol + 1 : NULL;
                continue;
            }
            /* TN is the record HEADER (precedes SF legitimately) and DDA
             * (the deleted-data marker) is a file-level cleanup note --
             * both always consumed. FN/FNDA/BRDA/LF/LH carry no
             * line-anchored hit model in v1 -- consumed when inside a
             * record, malformed before any SF: (the contracted
             * loud-failure list). Unknown keys record nothing (honest). */
            int is_header =
                (klen == 2 && memcmp(p, "TN", 2) == 0) || (klen == 3 && memcmp(p, "DDA", 3) == 0);
            int is_data = (klen == 4 && memcmp(p, "BRDA", 4) == 0) ||
                          (klen == 4 && memcmp(p, "FNDA", 4) == 0) ||
                          (klen == 2 && (memcmp(p, "FN", 2) == 0 || memcmp(p, "LF", 2) == 0 ||
                                         memcmp(p, "LH", 2) == 0));
            if (is_header || is_data) {
                if (is_data && !cur) {
                    rc = OC_ERR_USAGE;
                    oc_status_set(st, OC_ERR_USAGE,
                                  "lcov report '%s' line %zu: record key before any SF:", src_name,
                                  lineno);
                    break;
                }
                p = eol ? eol + 1 : NULL;
                continue;
            }
        }
        /* blank lines pass; anything else is not LCOV */
        size_t s = 0;
        while (s < linelen && (p[s] == ' ' || p[s] == '\t'))
            s++;
        if (s == linelen) {
            p = eol ? eol + 1 : NULL;
            continue;
        }
        rc = OC_ERR_USAGE;
        oc_status_set(st, OC_ERR_USAGE, "lcov report '%s' line %zu is not an lcov record", src_name,
                      lineno);
        break;
    }
    if (rc == OC_OK && cur) {
        rc = OC_ERR_USAGE;
        oc_status_set(st, OC_ERR_USAGE,
                      "lcov report '%s' ends inside an open record (missing end_of_record)",
                      src_name);
    }
    for (size_t k = 0; k < n_seen; k++)
        free(seen[k]);
    free(seen);
    return rc;
}

static int looks_like_lcov(const char *data, size_t len) {
    /* first non-blank line starts with "TN:" or "SF:" */
    size_t i = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && data[eol] != '\n')
            eol++;
        size_t s = i;
        while (s < eol && (data[s] == ' ' || data[s] == '\t' || data[s] == '\r'))
            s++;
        if (eol > s) {
            size_t lead = eol - s;
            if (lead >= 3 && memcmp(data + s, "SF:", 3) == 0)
                return 1;
            if (lead >= 3 && memcmp(data + s, "TN:", 3) == 0)
                return 1;
            return 0;
        }
        i = eol + 1;
    }
    return 0;
}

static char *xml_strip_comments_cov(oc_arena *a, const char *data, size_t n) {
    char *out = oc_arena_alloc(a, n + 1);
    if (!out)
        return NULL;
    size_t w = 0;
    for (size_t i = 0; i < n;) {
        if (i + 4 <= n && memcmp(data + i, "<!--", 4) == 0) {
            const char *end = strstr(data + i + 4, "-->");
            if (!end)
                break;
            i = (size_t)(end - data) + 3;
            continue;
        }
        out[w++] = data[i++];
    }
    out[w] = '\0';
    return out;
}

/* Value of attr="..." inside the element text starting at s (bounded by
 * the element's '>'); NULL when absent. */
static char *xml_attr_cov(oc_arena *a, const char *s, const char *attr) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s=\"", attr);
    const char *p = strstr(s, pat);
    if (!p)
        return NULL;
    p += strlen(pat);
    const char *q = strchr(p, '"');
    if (!q || q == p)
        return NULL;
    return oc_arena_strndup(a, p, (size_t)(q - p));
}

/* ------------------------------------------------------ SimpleCov ---- */

/* SimpleCov JSON adapter (SPRINT-16, docs/LANGUAGES.md Ruby section 6):
 * the `.resultset.json` format SimpleCov -- Ruby's de-facto coverage
 * tool -- emits: {"<suite>": {"coverage": {"<path>": {"lines": [...],
 * "branches": {...}}}, "timestamp": ...}}. The `lines` array is
 * positional (index i is line i+1): a NUMERIC entry is an anchor,
 * covered when the hit count > 0; a NULL entry is a not-relevant line
 * and is NOT an anchor (nothing fabricated for it); any other element
 * type is a USAGE error (loud, naming the resultset, suite and file).
 * Branches maps are consumed and skipped (no honest line-anchored hit
 * model in v1 -- the BRDA precedent). Suites union per file (the
 * istanbul multi-file discipline). */
static int looks_like_simplecov(const oc_json *root) {
    if (!root || root->type != OC_JSON_OBJ)
        return 0;
    for (size_t i = 0; i < root->n_fields; i++) {
        const oc_json *suite = root->vals[i];
        if (!suite || suite->type != OC_JSON_OBJ)
            continue;
        const oc_json *coverage = oc_json_get(suite, "coverage");
        if (coverage && coverage->type == OC_JSON_OBJ)
            return 1;
    }
    return 0;
}

static oc_code load_simplecov(oc_coverage *cov, const oc_json *root, const char *src_name,
                              oc_status *st) {
    for (size_t i = 0; i < root->n_fields; i++) {
        const oc_json *suite = root->vals[i];
        if (!suite || suite->type != OC_JSON_OBJ) {
            oc_status_set(st, OC_ERR_USAGE,
                          "simplecov resultset '%s': suite '%s' is not an "
                          "object (not a SimpleCov resultset?)",
                          src_name, root->keys[i] ? root->keys[i] : "?");
            return OC_ERR_USAGE;
        }
        const oc_json *coverage = oc_json_get(suite, "coverage");
        if (!coverage || coverage->type != OC_JSON_OBJ) {
            oc_status_set(st, OC_ERR_USAGE,
                          "simplecov resultset '%s': suite '%s' carries no coverage object",
                          src_name, root->keys[i] ? root->keys[i] : "?");
            return OC_ERR_USAGE;
        }
        for (size_t j = 0; j < coverage->n_fields; j++) {
            const oc_json *entry = coverage->vals[j];
            const char *fkey = coverage->keys[j] ? coverage->keys[j] : "?";
            if (!entry || entry->type != OC_JSON_OBJ) {
                oc_status_set(st, OC_ERR_USAGE,
                              "simplecov resultset '%s': suite '%s' file '%s' entry is not an "
                              "object",
                              src_name, root->keys[i], fkey);
                return OC_ERR_USAGE;
            }
            const oc_json *lines = oc_json_get(entry, "lines");
            if (!lines || lines->type != OC_JSON_ARR) {
                oc_status_set(st, OC_ERR_USAGE,
                              "simplecov resultset '%s': suite '%s' file '%s' carries no lines "
                              "array",
                              src_name, root->keys[i], fkey);
                return OC_ERR_USAGE;
            }
            /* pass 1: validate every element (numeric or null) and count */
            size_t na = 0, ncl = 0;
            for (size_t k = 0; k < lines->n_items; k++) {
                const oc_json *v = lines->items[k];
                if (!v || v->type == OC_JSON_NULL)
                    continue; /* not-relevant line: not an anchor */
                if (v->type != OC_JSON_NUM) {
                    oc_status_set(st, OC_ERR_USAGE,
                                  "simplecov resultset '%s': suite '%s' file '%s' line %zu is "
                                  "neither a hit count nor null",
                                  src_name, root->keys[i], fkey, k + 1);
                    return OC_ERR_USAGE;
                }
                na++;
                if (v->number > 0)
                    ncl++;
            }
            /* pass 2: fill the anchors (line = index + 1) */
            uint32_t *al = (uint32_t *)malloc((na ? na : 1) * sizeof(uint32_t));
            uint32_t *cl = (uint32_t *)malloc((ncl ? ncl : 1) * sizeof(uint32_t));
            if (!al || !cl) {
                free(al);
                free(cl);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory composing SimpleCov coverage");
                return OC_ERR_ALLOC;
            }
            size_t w1 = 0, w2 = 0;
            for (size_t k = 0; k < lines->n_items; k++) {
                const oc_json *v = lines->items[k];
                if (!v || v->type != OC_JSON_NUM)
                    continue;
                uint32_t ln = (uint32_t)(k + 1);
                al[w1++] = ln;
                if (v->number > 0)
                    cl[w2++] = ln;
            }
            oc_cov_file *e = cov_entry(cov, fkey);
            if (!e) {
                free(al);
                free(cl);
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
                return OC_ERR_ALLOC;
            }
            int ok = set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, al, na) &&
                     set_union(&e->covered, &e->n_covered, &e->cap_covered, cl, ncl);
            free(al);
            free(cl);
            if (!ok) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning coverage lines");
                return OC_ERR_ALLOC;
            }
            cov->files_loaded++;
        }
    }
    return OC_OK;
}

/* Coverlet Cobertura XML adapter (SPRINT-13, docs/LANGUAGES.md C#
 * section 6): the cobertura output of coverlet.msbuild / dotnet test.
 * Anchors are the LINE records (`<line number hits>` under `<class
 * filename="A/B/C.cs">`): a line is an anchor when the report carries
 * a record for it, covered when hits > 0 (Cobertura's own hit-count
 * semantics; the `branch` attribute is consumed exactly as far as the
 * line anchor needs it -- the line record is the finest honest
 * granularity Cobertura XML ships). The filename attribute maps
 * through the standard exact-then-unique-suffix rule. Comments are
 * stripped first; a malformed <line> record fails loud (never compose
 * silently). */
static oc_code load_cobertura(oc_coverage *cov, const char *data, size_t len, const char *src_name,
                              oc_arena *a, oc_status *st) {
    char *buf = xml_strip_comments_cov(a, data, len);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing Cobertura buffer");
        return OC_ERR_ALLOC;
    }
    char cur_file[800] = "";
    const char *p = buf;
    while (*p) {
        const char *lt = strchr(p, '<');
        if (!lt)
            break;
        p = lt + 1;
        if (strncmp(p, "class", 5) == 0 && (p[5] == ' ' || p[5] == '>')) {
            char *fn = xml_attr_cov(a, p, "filename");
            if (fn && fn[0]) {
                if (snprintf(cur_file, sizeof(cur_file), "%s", fn) >= (int)sizeof(cur_file)) {
                    oc_status_set(st, OC_ERR_USAGE,
                                  "Cobertura report '%s': class filename too long", src_name);
                    return OC_ERR_USAGE;
                }
                cov->files_loaded++; /* one entry per class element */
            }
            p = strchr(p, '>') ? strchr(p, '>') + 1 : p + 5;
        } else if (strncmp(p, "/class", 6) == 0) {
            cur_file[0] = '\0';
            p = strchr(p, '>') ? strchr(p, '>') + 1 : p + 6;
        } else if (strncmp(p, "line", 4) == 0 && (p[4] == ' ' || p[4] == '>' || p[4] == '/')) {
            const char *gt = strchr(p, '>');
            if (!gt)
                gt = p + 4;
            if (cur_file[0] == '\0') {
                /* line record outside a class: the reader does not
                 * recognize this shape -- skip it honestly (recorded
                 * in the contract) */
                p = gt + 1;
                continue;
            }
            char *num = xml_attr_cov(a, p, "number");
            char *hits = xml_attr_cov(a, p, "hits");
            if (!num) {
                oc_status_set(st, OC_ERR_USAGE,
                              "Cobertura report '%s': a <line> record without number", src_name);
                return OC_ERR_USAGE;
            }
            char *end = NULL;
            long line_no = strtol(num, &end, 10);
            if (end == num || *end != '\0' || line_no < 1) {
                oc_status_set(st, OC_ERR_USAGE, "Cobertura report '%s': malformed number '%s'",
                              src_name, num);
                return OC_ERR_USAGE;
            }
            long hit_cnt = hits ? strtol(hits, &end, 10) : 0;
            if (hits && (end == hits || *end != '\0')) {
                oc_status_set(st, OC_ERR_USAGE, "Cobertura report '%s': malformed hits '%s'",
                              src_name, hits);
                return OC_ERR_USAGE;
            }
            oc_cov_file *e = cov_entry(cov, cur_file);
            if (!e) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
                return OC_ERR_ALLOC;
            }
            uint32_t ln = (uint32_t)line_no;
            int ok = set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, &ln, 1);
            if (ok && hit_cnt > 0)
                ok = set_union(&e->covered, &e->n_covered, &e->cap_covered, &ln, 1);
            if (!ok) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning Cobertura lines");
                return OC_ERR_ALLOC;
            }
            p = gt + 1;
        } else {
            p = strchr(p, '>') ? strchr(p, '>') + 1 : p + 1;
        }
    }
    return OC_OK;
}

/* ---------------------------------------------------- Clover (PHP) ---- */

/* The Clover signature inside a <coverage> root (SPRINT-19): <file
 * name="..."> entries carrying <line num= ... count=> records. The other
 * <coverage>-rooted report -- Coverlet Cobertura (SPRINT-13) -- carries
 * <class filename=...> records and never a <file name= element, so the
 * sniff is exact; comments are stripped first so a commented-out block
 * cannot flip the sniff. */
static int looks_like_clover(const char *data, size_t len) {
    size_t i = 0;
    int saw_file_name = 0, saw_line_num = 0;
    while (i < len) {
        if (data[i] != '<') {
            i++;
            continue;
        }
        if (i + 6 <= len && memcmp(data + i, "<file ", 6) == 0) {
            const char *gt = memchr(data + i, '>', len - i);
            const char *end = gt ? gt : data + len;
            for (const char *q = data + i + 5; q + 6 <= end; q++)
                if (memcmp(q, "name=\"", 6) == 0)
                    saw_file_name = 1;
            if (gt)
                i = (size_t)(gt - data) + 1;
            else
                i = len;
            continue;
        }
        if (i + 6 <= len && memcmp(data + i, "<line ", 6) == 0) {
            const char *gt = memchr(data + i, '>', len - i);
            const char *end = gt ? gt : data + len;
            for (const char *q = data + i + 5; q + 5 <= end; q++)
                if (memcmp(q, "num=\"", 5) == 0)
                    saw_line_num = 1;
            if (gt)
                i = (size_t)(gt - data) + 1;
            else
                i = len;
            continue;
        }
        i++;
    }
    return saw_file_name && saw_line_num;
}

/* php-code-coverage Clover XML adapter (SPRINT-19, docs/LANGUAGES.md PHP
 * section 6): `<coverage>` -> `<file name=...>` entries with `<line num=
 * count=>` children. Every <line> record is an anchor (statement-level
 * records; Clover's own hit-count model), covered when count > 0,
 * uncovered when 0. The name attribute maps through the standard
 * exact-then-unique-suffix rule; a file entry whose path resolves to no
 * analyzed file is counted in the ambiguity meta, never guessed.
 * Comments are stripped first; a malformed <line> record fails loud
 * (never compose silently). Dispatched BEFORE the Cobertura reader on
 * the shared <coverage> root. */
static oc_code load_clover(oc_coverage *cov, const char *data, size_t len, const char *src_name,
                           oc_arena *a, oc_status *st) {
    char *buf = xml_strip_comments_cov(a, data, len);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing Clover buffer");
        return OC_ERR_ALLOC;
    }
    char cur_file[800] = "";
    const char *p = buf;
    while (*p) {
        const char *lt = strchr(p, '<');
        if (!lt)
            break;
        p = lt + 1;
        if (strncmp(p, "file ", 5) == 0 || strncmp(p, "file>", 5) == 0) {
            char *fn = xml_attr_cov(a, p, "name");
            if (fn && fn[0]) {
                if (snprintf(cur_file, sizeof(cur_file), "%s", fn) >= (int)sizeof(cur_file)) {
                    oc_status_set(st, OC_ERR_USAGE, "Clover report '%s': file name too long",
                                  src_name);
                    return OC_ERR_USAGE;
                }
                cov->files_loaded++; /* one entry per file element */
            }
            p = strchr(p, '>') ? strchr(p, '>') + 1 : p + 5;
        } else if (strncmp(p, "/file", 5) == 0) {
            cur_file[0] = '\0';
            p = strchr(p, '>') ? strchr(p, '>') + 1 : p + 5;
        } else if (strncmp(p, "line", 4) == 0 && (p[4] == ' ' || p[4] == '>' || p[4] == '/')) {
            const char *gt = strchr(p, '>');
            if (!gt)
                gt = p + 4;
            if (cur_file[0] == '\0') {
                /* line record outside a file: the reader does not
                 * recognize this shape -- skip it honestly (recorded
                 * in the contract) */
                p = gt + 1;
                continue;
            }
            char *num = xml_attr_cov(a, p, "num");
            char *cnt = xml_attr_cov(a, p, "count");
            if (!num) {
                oc_status_set(st, OC_ERR_USAGE, "Clover report '%s': a <line> record without num",
                              src_name);
                return OC_ERR_USAGE;
            }
            char *end = NULL;
            long line_no = strtol(num, &end, 10);
            if (end == num || *end != '\0' || line_no < 1) {
                oc_status_set(st, OC_ERR_USAGE, "Clover report '%s': malformed num '%s'", src_name,
                              num);
                return OC_ERR_USAGE;
            }
            long hit_cnt = cnt ? strtol(cnt, &end, 10) : 0;
            if (cnt && (end == cnt || *end != '\0')) {
                oc_status_set(st, OC_ERR_USAGE, "Clover report '%s': malformed count '%s'",
                              src_name, cnt);
                return OC_ERR_USAGE;
            }
            oc_cov_file *e = cov_entry(cov, cur_file);
            if (!e) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
                return OC_ERR_ALLOC;
            }
            uint32_t ln = (uint32_t)line_no;
            int ok = set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, &ln, 1);
            if (ok && hit_cnt > 0)
                ok = set_union(&e->covered, &e->n_covered, &e->cap_covered, &ln, 1);
            if (!ok) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning Clover lines");
                return OC_ERR_ALLOC;
            }
            p = gt + 1;
        } else {
            p = strchr(p, '>') ? strchr(p, '>') + 1 : p + 1;
        }
    }
    return OC_OK;
}

/* ---------------------------------------------------- OpenCover ------ */

/* OpenCover XML adapter (SPRINT-14, docs/LANGUAGES.md C# section 6): the
 * "different schema" descope is closed -- OpenCover carries line-anchored
 * statement records, the same honest shape as the Go coverprofile block.
 * Anchors are the <SequencePoint> records: the INCLUSIVE sl..el line
 * range (a sequence point IS the statement unit OpenCover measures),
 * covered when vc > 0; each record joins to <File uid fullPath> through
 * its fileid attribute, and the fullPath maps through the standard
 * exact-then-unique-suffix rule; a record whose fileid names no known
 * file records nothing (honest). Comments are stripped first; a
 * malformed record fails loud (never compose silently). */
static oc_code load_opencover(oc_coverage *cov, const char *data, size_t len, const char *src_name,
                              oc_arena *a, oc_status *st) {
    char *buf = xml_strip_comments_cov(a, data, len);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing OpenCover buffer");
        return OC_ERR_ALLOC;
    }
    /* file table: uid -> fullPath (the join target of every record) */
    typedef struct {
        long uid;
        const char *path;
    } ocf;
    size_t cap_f = 32, n_f = 0;
    ocf *ftab = (ocf *)malloc(cap_f * sizeof(ocf));
    if (!ftab) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing OpenCover file table");
        return OC_ERR_ALLOC;
    }
    const char *p = buf;
    oc_code rc = OC_OK;
    while (*p && rc == OC_OK) {
        const char *lt = strchr(p, '<');
        if (!lt)
            break;
        p = lt + 1;
        if (strncmp(p, "File", 4) == 0 && (p[4] == ' ' || p[4] == '/' || p[4] == '>')) {
            const char *gt = strchr(p, '>');
            if (!gt)
                gt = p + 4;
            char *uid = xml_attr_cov(a, p, "uid");
            char *full = xml_attr_cov(a, p, "fullPath");
            if (uid && full && uid[0] && full[0]) {
                char *end = NULL;
                long u = strtol(uid, &end, 10);
                if (end != uid && *end == '\0' && u >= 0) {
                    if (n_f == cap_f) {
                        size_t ncap = cap_f * 2;
                        ocf *grown = (ocf *)realloc(ftab, ncap * sizeof(ocf));
                        if (!grown) {
                            oc_status_set(st, OC_ERR_ALLOC,
                                          "out of memory storing OpenCover file table");
                            rc = OC_ERR_ALLOC;
                            break;
                        }
                        ftab = grown;
                        cap_f = ncap;
                    }
                    ftab[n_f].uid = u;
                    ftab[n_f].path = strdup(full);
                    if (!ftab[n_f].path) {
                        oc_status_set(st, OC_ERR_ALLOC,
                                      "out of memory storing OpenCover file path");
                        rc = OC_ERR_ALLOC;
                        break;
                    }
                    n_f++;
                    cov->files_loaded++; /* one entry per <File> element */
                }
            }
            p = gt + 1;
        } else if (strncmp(p, "SequencePoint", 13) == 0 &&
                   (p[13] == ' ' || p[13] == '/' || p[13] == '>')) {
            const char *gt = strchr(p, '>');
            if (!gt)
                gt = p + 13;
            char *vc = xml_attr_cov(a, p, "vc");
            char *sl = xml_attr_cov(a, p, "sl");
            char *el = xml_attr_cov(a, p, "el");
            char *fid = xml_attr_cov(a, p, "fileid");
            if (!sl || !vc || !el || !fid) {
                oc_status_set(st, OC_ERR_USAGE,
                              "OpenCover report '%s': a <SequencePoint> without "
                              "vc/sl/el/fileid",
                              src_name);
                rc = OC_ERR_USAGE;
                break;
            }
            char *end = NULL;
            long start_l = strtol(sl, &end, 10);
            if (end == sl || *end != '\0' || start_l < 1) {
                oc_status_set(st, OC_ERR_USAGE, "OpenCover report '%s': malformed sl '%s'",
                              src_name, sl);
                rc = OC_ERR_USAGE;
                break;
            }
            long end_l = strtol(el, &end, 10);
            if (end == el || *end != '\0' || end_l < start_l) {
                oc_status_set(st, OC_ERR_USAGE, "OpenCover report '%s': malformed el '%s'",
                              src_name, el);
                rc = OC_ERR_USAGE;
                break;
            }
            long hits = strtol(vc, &end, 10);
            if (end == vc || *end != '\0') {
                oc_status_set(st, OC_ERR_USAGE, "OpenCover report '%s': malformed vc '%s'",
                              src_name, vc);
                rc = OC_ERR_USAGE;
                break;
            }
            long u = strtol(fid, &end, 10);
            const char *fpath = NULL;
            if (end != fid && *end == '\0') {
                for (size_t k = 0; k < n_f; k++) {
                    if (ftab[k].uid == u) {
                        fpath = ftab[k].path;
                        break;
                    }
                }
            }
            if (fpath) {
                oc_cov_file *e = cov_entry(cov, fpath);
                if (!e) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory storing coverage entry");
                    rc = OC_ERR_ALLOC;
                    break;
                }
                uint32_t lines[256];
                size_t span = (size_t)(end_l - start_l + 1);
                if (span > 256) {
                    oc_status_set(st, OC_ERR_USAGE,
                                  "OpenCover report '%s': a <SequencePoint> spanning more "
                                  "than 256 lines",
                                  src_name);
                    rc = OC_ERR_USAGE;
                    break;
                }
                for (size_t l = 0; l < span; l++)
                    lines[l] = (uint32_t)(start_l + (long)l);
                if (!set_union(&e->anchor, &e->n_anchor, &e->cap_anchor, lines, span)) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning OpenCover lines");
                    rc = OC_ERR_ALLOC;
                    break;
                }
                if (hits > 0 &&
                    !set_union(&e->covered, &e->n_covered, &e->cap_covered, lines, span)) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory unioning OpenCover lines");
                    rc = OC_ERR_ALLOC;
                    break;
                }
            }
            /* fileid naming no known file: the record joins to nothing --
             * recorded nothing, honest */
            p = gt + 1;
        } else {
            p = strchr(p, '>') ? strchr(p, '>') + 1 : p + 1;
        }
    }
    for (size_t k = 0; k < n_f; k++)
        free((void *)ftab[k].path);
    free(ftab);
    return rc;
}

/* ------------------------------------------------------------- load --- */

static int looks_like_xml(const char *data, size_t len) {
    size_t i = 0;
    while (i < len && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n'))
        i++;
    return i + 5 <= len && memcmp(data + i, "<?xml", 5) == 0;
}

static int looks_like_go_coverprofile(const char *data, size_t len) {
    /* first non-blank line starts with "mode:" */
    size_t i = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && data[eol] != '\n')
            eol++;
        size_t s = i;
        while (s < eol && (data[s] == ' ' || data[s] == '\t' || data[s] == '\r'))
            s++;
        if (eol > s) {
            size_t lead = eol - s;
            return lead >= 5 && memcmp(data + s, "mode:", 5) == 0;
        }
        i = eol + 1;
    }
    return 0;
}

/* The ROOT ELEMENT decides which XML reader consumes the report
 * (SPRINT-13/14): `<report>` is JaCoCo (SPRINT-12), `<coverage>` is
 * Coverlet Cobertura (SPRINT-13), `<CoverageSession>` is OpenCover
 * (SPRINT-14); anything else falls to the JaCoCo element scan, which
 * recognizes none of its shapes and records nothing -- honest.
 * Processing instructions, comments and the DOCTYPE line are skipped
 * before the sniff. */
static const char *xml_root_element(const char *data, size_t len) {
    static char tag[64];
    size_t i = 0;
    while (i < len) {
        if (data[i] != '<') {
            i++;
            continue;
        }
        if (i + 4 <= len && memcmp(data + i, "<!--", 4) == 0) {
            /* a comment may contain '>' -- its terminator is '-->' */
            const char *end = strstr(data + i + 4, "-->");
            if (!end)
                return NULL;
            i = (size_t)(end - data) + 3;
            continue;
        }
        if (i + 1 < len && (data[i + 1] == '?' || data[i + 1] == '!')) {
            /* processing instruction or DOCTYPE: no nested '>' */
            const char *gt = memchr(data + i, '>', len - i);
            if (!gt)
                return NULL;
            i = (size_t)(gt - data) + 1;
            continue;
        }
        /* the first real element */
        i++;
        size_t t = 0;
        while (i < len && t + 1 < sizeof(tag) &&
               (data[i] == '_' || data[i] == '-' || (data[i] >= 'a' && data[i] <= 'z') ||
                (data[i] >= 'A' && data[i] <= 'Z') || (data[i] >= '0' && data[i] <= '9')))
            tag[t++] = data[i++];
        tag[t] = '\0';
        return t ? tag : NULL;
    }
    return NULL;
}

oc_code oc_coverage_load(oc_coverage *cov, const char *const *paths, size_t n, oc_arena *a,
                         oc_status *st) {
    for (size_t i = 0; i < n; i++) {
        char *data = NULL;
        size_t len = 0;
        oc_code rc = oc_read_file(paths[i], &data, &len, st);
        if (rc != OC_OK)
            return rc;
        if (looks_like_xml(data, len)) {
            const char *root = xml_root_element(data, len);
            /* SPRINT-19: BOTH Clover (php-code-coverage) and Coverlet
             * Cobertura root at <coverage> -- the Clover signature (<file
             * name= entries carrying <line num=> records) is sniffed
             * FIRST and dispatched before the Cobertura reader (the
             * contract's "before the istanbul fallback" ordering on the
             * XML branch); a <coverage> doc without the signature stays
             * Cobertura exactly as before. */
            if (root && strcmp(root, "coverage") == 0 && looks_like_clover(data, len))
                rc = load_clover(cov, data, len, paths[i], a, st);
            else if (root && strcmp(root, "coverage") == 0)
                rc = load_cobertura(cov, data, len, paths[i], a, st);
            else if (root && strcmp(root, "CoverageSession") == 0)
                rc = load_opencover(cov, data, len, paths[i], a, st);
            else
                rc = load_jacoco(cov, data, len, a, st);
            free(data);
            if (rc != OC_OK)
                return rc;
            continue;
        }
        if (looks_like_go_coverprofile(data, len)) {
            rc = load_go_coverprofile(cov, data, paths[i], st);
            free(data);
            if (rc != OC_OK)
                return rc;
            continue;
        }
        if (looks_like_lcov(data, len)) {
            rc = load_lcov(cov, data, paths[i], st);
            free(data);
            if (rc != OC_OK)
                return rc;
            continue;
        }
        oc_json *root = oc_json_parse(a, data, len, st);
        free(data);
        if (!root || root->type != OC_JSON_OBJ) {
            oc_status_set(st, OC_ERR_USAGE, "coverage file '%s' is not a JSON object", paths[i]);
            return OC_ERR_USAGE;
        }
        /* SimpleCov first (SPRINT-16, Ruby): the suite-of-coverage shape
         * is distinct from istanbul (whose top-level entries carry
         * statementMap/s) and from V8 ({"result": ...}) and coverage.py
         * ({"files": ...}). */
        rc = looks_like_simplecov(root)    ? load_simplecov(cov, root, paths[i], st)
             : oc_json_get(root, "result") ? load_v8(cov, root, a, st)
             : oc_json_get(root, "files")  ? load_pycov(cov, root, st)
                                           : load_istanbul(cov, root, st);
        if (rc != OC_OK)
            return rc;
    }
    /* Deterministic order for suffix matching + report meta. (M-10/T16
     * hygiene: guard qsort against a NULL base on zero files -- UB.) */
    if (cov->n_files > 0)
        qsort(cov->files, cov->n_files, sizeof(oc_cov_file), cmp_cov_file);
    return OC_OK;
}

/* ------------------------------------------------------------ lookup -- */

/* The standard path rule, both directions (SPRINT-11 JaCoCo note):
 *   - exact match;
 *   - the ANALYZED path is a "/"-boundary suffix of the report path
 *     (istanbul/V8/coverage.py reports usually carry repo-rooted or
 *     absolute paths);
 *   - the REPORT path is a "/"-boundary suffix of the analyzed path --
 *     JaCoCo XML names sourcefiles package-relative
 *     ("com/example/util/TextUtil.java"), which is the analyzed path's
 *     tail. Ambiguous tails match nothing (the CLI meta walk counts
 *     them); an edge to coverage is never guessed. */
static int cov_matches_analyzed(const oc_cov_file *e, const char *analyzed) {
    if (strcmp(e->path, analyzed) == 0)
        return 1;
    size_t pl = strlen(e->path), al = strlen(analyzed);
    if (al == 0 || pl == 0)
        return 0;
    if (al > pl && analyzed[al - pl - 1] == '/' && memcmp(analyzed + al - pl, e->path, pl) == 0)
        return 1; /* report path is the analyzed path's package tail */
    if (al + 1 > pl)
        return 0;
    return e->path[pl - al - 1] == '/' && memcmp(e->path + pl - al, analyzed, al) == 0;
}

int oc_coverage_lookup(const oc_coverage *cov, const char *analyzed_path, size_t def_start,
                       size_t def_end, size_t *anchors_out, size_t *covered_out) {
    *anchors_out = 0;
    *covered_out = 0;
    if (!cov || cov->n_files == 0 || def_start == 0 || def_end < def_start)
        return 0;
    /* Exact path first: binary search over the sorted array. */
    size_t lo = 0, hi = cov->n_files;
    int exact = 0;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(cov->files[mid].path, analyzed_path);
        if (c == 0) {
            exact = 1;
            lo = mid;
            break;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (!exact) {
        /* Unique "/"-boundary suffix match; ambiguous suffixes match
         * nothing (counted in files_ambiguous by the CLI meta walk). */
        size_t hits = 0, hit_idx = 0;
        for (size_t i = 0; i < cov->n_files; i++) {
            if (cov_matches_analyzed(&cov->files[i], analyzed_path)) {
                hits++;
                hit_idx = i;
            }
        }
        if (hits != 1)
            return 0;
        lo = hit_idx;
    }
    oc_cov_file *e = &cov->files[lo];
    /* Both arrays are sorted: bounded scans. */
    size_t anchors = 0, covered = 0;
    for (size_t i = 0; i < e->n_anchor; i++) {
        size_t ln = e->anchor[i];
        if (ln < def_start)
            continue;
        if (ln > def_end)
            break;
        anchors++;
    }
    for (size_t i = 0; i < e->n_covered; i++) {
        size_t ln = e->covered[i];
        if (ln < def_start)
            continue;
        if (ln > def_end)
            break;
        covered++;
    }
    *anchors_out = anchors;
    *covered_out = covered;
    return 1;
}

/* Marks which coverage entries were mapped onto analyzed files; call
 * once from the CLI after loading (lookup is const and never mutates). */
void oc_coverage_mark_matches(oc_coverage *cov, const oc_graph *g) {
    if (!cov)
        return;
    cov->files_matched = 0;
    for (size_t i = 0; i < cov->n_files; i++)
        cov->files[i].matched = 0;
    for (size_t i = 0; i < cov->n_files; i++) {
        for (size_t j = 0; j < g->n_nodes; j++) {
            if (g->nodes[j].kind == OC_NODE_FILE &&
                cov_matches_analyzed(&cov->files[i], g->nodes[j].path)) {
                cov->files[i].matched = 1;
                cov->files_matched++;
                break;
            }
        }
    }
}
