/* OC008 duplicate-code index -- see include/oc/dup.h for the contract.
 *
 * Implementation notes:
 *  - All retained data lives in the caller's arena. The only heap
 *    allocations are build-time scratch (file buffers, the window
 *    array, the span workspace) and every path frees it before return.
 *  - No finding ever rests on a fingerprint: candidate windows are
 *    bucketed by a 64-bit rolling hash but verified and extended by
 *    direct text comparison, and the final grouping re-verifies the
 *    full span text. Deterministic on identical inputs by construction
 *    (sorted arrays, stable tie-breaks, no map iteration order).
 *  - Cost on the 10k-file perf corpus is dominated by the window sort
 *    (~0.7M entries); lockstep extension is bounded by (seeds x span
 *    lines x members).
 */
#include "oc/dup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ util */

static uint64_t fnv1a(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int is_space_byte(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

/* Normalize one physical line into an arena string: strip CR, trim,
 * collapse internal whitespace runs to a single space. Empty input
 * yields "" (kept -- spans count physical lines). */
static char *dup_normalize_line(oc_arena *a, const char *s, size_t len) {
    size_t b = 0, e = len;
    while (b < e && is_space_byte(s[b]))
        b++;
    while (e > b && is_space_byte(s[e - 1]))
        e--;
    char *out = (char *)oc_arena_alloc(a, e - b + 1);
    if (!out)
        return NULL;
    size_t o = 0;
    int prev_space = 0;
    for (size_t i = b; i < e; i++) {
        char c = s[i];
        if (is_space_byte(c)) {
            if (prev_space)
                continue;
            out[o++] = ' ';
            prev_space = 1;
        } else {
            out[o++] = c;
            prev_space = 0;
        }
    }
    out[o] = '\0';
    return out;
}

typedef struct {
    uint64_t h;
    size_t file_idx;
    size_t start; /* 0-based first line of the window */
} dup_window;

static int cmp_window(const void *pa, const void *pb) {
    const dup_window *x = (const dup_window *)pa, *y = (const dup_window *)pb;
    if (x->h != y->h)
        return x->h < y->h ? -1 : 1;
    if (x->file_idx != y->file_idx)
        return x->file_idx < y->file_idx ? -1 : 1;
    if (x->start != y->start)
        return x->start < y->start ? -1 : 1;
    return 0;
}

typedef struct {
    size_t file_idx;
    size_t start; /* 0-based, of the EXTENDED span */
    size_t end;   /* 0-based exclusive */
} dup_span;

typedef struct {
    uint64_t hash;
    dup_span sp;
} span_entry;

static int cmp_span_entry(const void *pa, const void *pb) {
    const span_entry *x = (const span_entry *)pa, *y = (const span_entry *)pb;
    if (x->hash != y->hash)
        return x->hash < y->hash ? -1 : 1;
    size_t lx = x->sp.end - x->sp.start, ly = y->sp.end - y->sp.start;
    if (lx != ly)
        return lx < ly ? -1 : 1;
    if (x->sp.file_idx != y->sp.file_idx)
        return x->sp.file_idx < y->sp.file_idx ? -1 : 1;
    if (x->sp.start != y->sp.start)
        return x->sp.start < y->sp.start ? -1 : 1;
    return 0;
}

/* Order two finalized groups by (first-member path, first-member start,
 * first-member end); the group's first member is its location minimum
 * because members emerge from the span sort already ordered and file_idx
 * order == path order. */
static int group_before(const oc_dup_file *files, const oc_dup_group *a, const oc_dup_group *b) {
    const char *pa = files[a->members[0].file_idx].path;
    const char *pb = files[b->members[0].file_idx].path;
    int c = strcmp(pa, pb);
    if (c != 0)
        return c > 0;
    if (a->members[0].start != b->members[0].start)
        return a->members[0].start > b->members[0].start;
    return a->members[0].end > b->members[0].end;
}

/* ------------------------------------------------------------------ main */

oc_dup_index *oc_dup_build(const oc_graph *g, const oc_config *cfg, oc_arena *a, oc_status *st) {
    long long min_block = oc_config_get_int(cfg, "OC008.min_block", OC_DUP_DEFAULT_MIN_BLOCK);
    long long max_file_bytes =
        oc_config_get_int(cfg, "OC008.max_file_bytes", OC_DUP_DEFAULT_MAX_FILE_BYTES);
    long long max_groups = oc_config_get_int(cfg, "OC008.max_groups", OC_DUP_DEFAULT_MAX_GROUPS);
    if (min_block < 2)
        min_block = 2;
    if (max_file_bytes < 1)
        max_file_bytes = 1;
    if (max_groups < 1)
        max_groups = 1;
    if (max_groups > 65536)
        max_groups = 65536; /* arena guard against silly config values */
    const size_t K = (size_t)min_block;

    /* All scratch, declared and initialized once so every failure path
     * can free through one label. */
    size_t *line_counts = NULL;
    uint64_t **line_hashes = NULL;
    const char ***line_texts = NULL;
    dup_window *wins = NULL;
    span_entry *spans = NULL;
    size_t n_wins = 0, cap_wins = 0;
    size_t n_spans = 0, cap_spans = 0;
    size_t n_files = 0;

    oc_dup_index *idx = (oc_dup_index *)oc_arena_alloc(a, sizeof(*idx));
    if (!idx) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building duplication index");
        return NULL; /* nothing to free yet */
    }
    memset(idx, 0, sizeof(*idx));

    line_counts = (size_t *)calloc(1, sizeof(size_t));
    line_hashes = (uint64_t **)calloc(1, sizeof(uint64_t *));
    line_texts = (const char ***)calloc(1, sizeof(const char **));
    if (!line_counts || !line_hashes || !line_texts)
        goto fail;

    /* 1. File set: every file node, sorted by path. */
    for (size_t i = 0; i < g->n_nodes; i++)
        if (g->nodes[i].kind == OC_NODE_FILE)
            n_files++;
    idx->files = (oc_dup_file *)oc_arena_alloc(a, (n_files ? n_files : 1) * sizeof(oc_dup_file));
    if (!idx->files)
        goto fail;
    {
        size_t fi = 0;
        for (size_t i = 0; i < g->n_nodes; i++) {
            if (g->nodes[i].kind != OC_NODE_FILE)
                continue;
            idx->files[fi].node_id = i;
            idx->files[fi].path = g->nodes[i].path;
            fi++;
        }
    }
    idx->n_files = n_files;
    for (size_t i = 1; i < idx->n_files; i++) {
        oc_dup_file tmp = idx->files[i];
        size_t j = i;
        while (j > 0 && strcmp(idx->files[j - 1].path, tmp.path) > 0) {
            idx->files[j] = idx->files[j - 1];
            j--;
        }
        idx->files[j] = tmp;
    }

    free(line_counts);
    free(line_hashes);
    free(line_texts);
    line_counts = (size_t *)calloc(n_files ? n_files : 1, sizeof(size_t));
    line_hashes = (uint64_t **)calloc(n_files ? n_files : 1, sizeof(uint64_t *));
    line_texts = (const char ***)calloc(n_files ? n_files : 1, sizeof(const char **));
    if (!line_counts || !line_hashes || !line_texts)
        goto fail;

    /* 2. Normalized lines per file. line_texts live in the arena; the
     * hash arrays are heap scratch. Files larger than the budget are
     * skipped and counted (their slots stay NULL / 0). */
    for (size_t f = 0; f < n_files; f++) {
        char *data = NULL;
        size_t len = 0;
        if (oc_read_file(idx->files[f].path, &data, &len, st) != OC_OK)
            goto fail; /* the graph analyzed this file, so a failed
                        * re-read is a hard tool error; st carries it */
        if (len > (size_t)max_file_bytes) {
            idx->files_skipped_oversize++;
            free(data);
            continue;
        }
        size_t n_lines = 0;
        for (size_t i = 0; i < len; i++)
            if (data[i] == '\n')
                n_lines++;
        if (len > 0 && data[len - 1] != '\n')
            n_lines++;
        if (n_lines == 0) {
            free(data);
            continue;
        }
        line_texts[f] = (const char **)oc_arena_alloc(a, n_lines * sizeof(char *));
        line_hashes[f] = (uint64_t *)malloc(n_lines * sizeof(uint64_t));
        if (!line_texts[f] || !line_hashes[f]) {
            free(data);
            goto fail;
        }
        size_t li = 0, ls = 0;
        for (size_t i = 0; i <= len; i++) {
            if (i == len || data[i] == '\n') {
                if (i > ls) {
                    char *norm = dup_normalize_line(a, data + ls, i - ls);
                    if (!norm) {
                        free(data);
                        goto fail;
                    }
                    line_texts[f][li] = norm;
                } else if (i < len) {
                    line_texts[f][li] = ""; /* empty physical line */
                } else {
                    break; /* trailing newline: no phantom line */
                }
                line_hashes[f][li] = fnv1a(line_texts[f][li], strlen(line_texts[f][li]));
                li++;
                ls = i + 1;
            }
        }
        line_counts[f] = li;
        free(data);
    }

    /* 3. Seed windows, sorted for run detection. */
    for (size_t f = 0; f < n_files; f++) {
        size_t n = line_counts[f];
        if (n < K)
            continue;
        for (size_t s = 0; s + K <= n; s++) {
            uint64_t h = 1469598103934665603ULL;
            for (size_t j = 0; j < K; j++) {
                h ^= line_hashes[f][s + j];
                h *= 1099511628211ULL;
            }
            if (n_wins == cap_wins) {
                size_t ncap = cap_wins ? cap_wins * 2 : 4096;
                dup_window *nw = (dup_window *)realloc(wins, ncap * sizeof(dup_window));
                if (!nw)
                    goto fail;
                wins = nw;
                cap_wins = ncap;
            }
            wins[n_wins].h = h;
            wins[n_wins].file_idx = f;
            wins[n_wins].start = s;
            n_wins++;
        }
    }
    qsort(wins, n_wins, sizeof(dup_window), cmp_window);

    /* 4. Candidate runs: verify text, lockstep-extend, emit spans. */
    size_t wi = 0;
    while (wi < n_wins) {
        size_t wj = wi;
        while (wj < n_wins && wins[wj].h == wins[wi].h)
            wj++;
        size_t run = wj - wi;
        if (run >= 2) {
            const dup_window *first = &wins[wi];
            size_t *member_idx = (size_t *)malloc(run * sizeof(size_t));
            if (!member_idx)
                goto fail;
            size_t verified = 0;
            for (size_t m = 0; m < run; m++) {
                const dup_window *w = &wins[wi + m];
                const char **ta = line_texts[first->file_idx];
                const char **tb = line_texts[w->file_idx];
                if (!ta || !tb)
                    continue;
                int same = 1;
                for (size_t t = 0; t < K; t++) {
                    if (strcmp(ta[first->start + t], tb[w->start + t]) != 0) {
                        same = 0;
                        break;
                    }
                }
                if (same)
                    member_idx[verified++] = wi + m;
            }
            if (verified >= 2) {
                dup_span *sp = (dup_span *)malloc(verified * sizeof(dup_span));
                if (!sp) {
                    free(member_idx);
                    goto fail;
                }
                for (size_t m = 0; m < verified; m++) {
                    const dup_window *w = &wins[member_idx[m]];
                    sp[m].file_idx = w->file_idx;
                    sp[m].start = w->start;
                    sp[m].end = w->start + K;
                }
                /* Lockstep forward extension: the candidate is the next
                 * line of the first member; every member must carry the
                 * same text at the same offset relative to its own span. */
                for (int go = 1; go;) {
                    go = 0;
                    const dup_window *w0 = &wins[member_idx[0]];
                    if (sp[0].end >= line_counts[w0->file_idx])
                        break;
                    const char *cand = line_texts[w0->file_idx][sp[0].end];
                    int all = 1;
                    for (size_t m = 1; m < verified; m++) {
                        const dup_window *wm = &wins[member_idx[m]];
                        if (sp[m].end >= line_counts[wm->file_idx] ||
                            strcmp(cand, line_texts[wm->file_idx][sp[m].end]) != 0) {
                            all = 0;
                            break;
                        }
                    }
                    if (all) {
                        for (size_t m = 0; m < verified; m++)
                            sp[m].end++;
                        go = 1;
                    }
                }
                /* Lockstep backward extension. */
                for (int go = 1; go;) {
                    go = 0;
                    const dup_window *w0 = &wins[member_idx[0]];
                    if (sp[0].start == 0)
                        break;
                    const char *cand = line_texts[w0->file_idx][sp[0].start - 1];
                    int all = 1;
                    for (size_t m = 1; m < verified; m++) {
                        const dup_window *wm = &wins[member_idx[m]];
                        if (sp[m].start == 0 ||
                            strcmp(cand, line_texts[wm->file_idx][sp[m].start - 1]) != 0) {
                            all = 0;
                            break;
                        }
                    }
                    if (all) {
                        for (size_t m = 0; m < verified; m++)
                            sp[m].start--;
                        go = 1;
                    }
                }
                for (size_t m = 0; m < verified; m++) {
                    uint64_t h = 1469598103934665603ULL;
                    for (size_t t = sp[m].start; t < sp[m].end; t++) {
                        h ^= line_hashes[sp[m].file_idx][t];
                        h *= 1099511628211ULL;
                    }
                    if (n_spans == cap_spans) {
                        size_t ncap = cap_spans ? cap_spans * 2 : 256;
                        span_entry *ns = (span_entry *)realloc(spans, ncap * sizeof(span_entry));
                        if (!ns) {
                            free(sp);
                            free(member_idx);
                            goto fail;
                        }
                        spans = ns;
                        cap_spans = ncap;
                    }
                    spans[n_spans].hash = h;
                    spans[n_spans].sp = sp[m];
                    n_spans++;
                }
                free(sp);
            }
            free(member_idx);
        }
        wi = wj;
    }

    /* 5. Final grouping: identical spans (one location converged from
     * several seeds) collapse in the sort; a dup group is a run of
     * >= 2 entries with equal hash AND equal span length AND equal span
     * text (re-verified line by line -- never trusted from the hash). */
    if (n_spans > 1)
        qsort(spans, n_spans, sizeof(span_entry), cmp_span_entry);

    /* 5a. Collapse identical spans: seeds inside one maximal block
     * converge to the same (file, start, end); keep one per location.
     * Equal-hash runs are adjacent after the sort, and a location's
     * span hash is derived from its own lines, so identical locations
     * are always adjacent here. */
    {
        size_t u = 0;
        for (size_t t = 0; t < n_spans; t++) {
            if (u > 0 && spans[t].sp.file_idx == spans[u - 1].sp.file_idx &&
                spans[t].sp.start == spans[u - 1].sp.start &&
                spans[t].sp.end == spans[u - 1].sp.end)
                continue;
            spans[u++] = spans[t];
        }
        n_spans = u;
    }

    size_t cap_groups = (size_t)max_groups;
    idx->groups = (oc_dup_group *)oc_arena_alloc(a, cap_groups * sizeof(oc_dup_group));
    if (!idx->groups)
        goto fail;

    size_t s = 0;
    while (s < n_spans) {
        size_t e = s;
        size_t len = spans[s].sp.end - spans[s].sp.start;
        while (e < n_spans && spans[e].hash == spans[s].hash &&
               (spans[e].sp.end - spans[e].sp.start) == len)
            e++;
        if (e - s >= 2) {
            /* text-verify every member against the run's first span,
             * compacting survivors to the front of the run */
            size_t kept = 0;
            const dup_span *ref = &spans[s].sp;
            const char **rt = line_texts[ref->file_idx];
            for (size_t m = s; m < e; m++) {
                const dup_span *cur = &spans[m].sp;
                const char **ct = line_texts[cur->file_idx];
                if (!rt || !ct)
                    continue;
                int same = 1;
                for (size_t t = 0; t < len; t++) {
                    if (strcmp(rt[ref->start + t], ct[cur->start + t]) != 0) {
                        same = 0;
                        break;
                    }
                }
                if (!same)
                    continue;
                spans[s + kept] = spans[m];
                kept++;
            }
            if (kept >= 2) {
                if (idx->n_groups < cap_groups) {
                    oc_dup_group *grp = &idx->groups[idx->n_groups++];
                    grp->members = (oc_dup_member *)oc_arena_alloc(a, kept * sizeof(oc_dup_member));
                    if (!grp->members)
                        goto fail;
                    grp->n_members = kept;
                    grp->span_lines = len;
                    grp->text_hash = spans[s].hash;
                    for (size_t m = 0; m < kept; m++) {
                        grp->members[m].file_idx = spans[s + m].sp.file_idx;
                        grp->members[m].start = spans[s + m].sp.start + 1; /* 1-based */
                        grp->members[m].end = spans[s + m].sp.end;         /* inclusive */
                    }
                    /* Shared message; locations are already sorted by
                     * (path, start). Buffer sized from the real path
                     * lengths so a listed location never truncates;
                     * snprintf bounds keep it safe regardless. */
                    {
                        size_t list = kept < 8 ? kept : 8;
                        size_t path_budget = 0;
                        for (size_t m = 0; m < list; m++) {
                            size_t pl = strlen(idx->files[grp->members[m].file_idx].path);
                            if (pl > path_budget)
                                path_budget = pl;
                        }
                        size_t cap = 96 + list * (path_budget + 32) + 64;
                        char *msgbuf = (char *)oc_arena_alloc(a, cap);
                        if (!msgbuf)
                            goto fail;
                        size_t off = 0;
                        int wr = snprintf(msgbuf, cap,
                                          "Normalized %zu-line block duplicated at %zu "
                                          "location(s): ",
                                          len, kept);
                        if (wr < 0)
                            goto fail;
                        off = (size_t)wr < cap - 1 ? (size_t)wr : cap - 1;
                        for (size_t m = 0; m < list && off < cap - 1; m++) {
                            const oc_dup_member *mm = &grp->members[m];
                            wr = snprintf(msgbuf + off, cap - off, "%s%s:%zu-%zu", m ? ", " : "",
                                          idx->files[mm->file_idx].path, mm->start, mm->end);
                            if (wr < 0)
                                break;
                            if ((size_t)wr >= cap - off) {
                                off = cap - 1;
                                break;
                            }
                            off += (size_t)wr;
                        }
                        if (kept > list && off < cap - 1)
                            snprintf(msgbuf + off, cap - off, " (+%zu more)", kept - list);
                        grp->message = msgbuf;
                    }
                } else {
                    idx->groups_truncated++;
                }
            }
        }
        s = e;
    }

    /* Location-order the groups (small count; insertion sort). */
    for (size_t i = 1; i < idx->n_groups; i++) {
        oc_dup_group tmp = idx->groups[i];
        size_t j = i;
        while (j > 0 && group_before(idx->files, &idx->groups[j - 1], &tmp)) {
            idx->groups[j] = idx->groups[j - 1];
            j--;
        }
        idx->groups[j] = tmp;
    }

    free(spans);
    free(wins);
    for (size_t f = 0; f < n_files; f++)
        free(line_hashes[f]);
    free(line_hashes);
    free(line_texts);
    free(line_counts);
    oc_status_clear(st);
    return idx;

fail:
    free(spans);
    free(wins);
    if (line_hashes) {
        for (size_t f = 0; f < n_files; f++)
            free(line_hashes[f]);
    }
    free(line_hashes);
    free(line_texts);
    free(line_counts);
    if (st->code == OC_OK)
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building duplication index");
    return NULL;
}
