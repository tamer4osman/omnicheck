/* Sprint 5 (M4 RC scope) tests: supply-chain tier (WBS 4.2), V8
 * coverage adapter + CRAP leaderboard (WBS 4.3), mutation engine
 * (WBS 4.5), breaking-change gate (WBS 4.6). Units run against the
 * library; end-to-end checks run the real oc binary (like test_cli). */
#include "oc/compat.h"
#include "oc/coverage.h"
#include "oc/finding.h"
#include "oc/json.h"
#include "oc/mutate.h"
#include "oc/supply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int check_count = 0;
static int check_fail = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        check_count++;                                                                             \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
            check_fail++;                                                                          \
        }                                                                                          \
    } while (0)
#define CHECK_STR_EQ(a, b)                                                                         \
    do {                                                                                           \
        check_count++;                                                                             \
        if (strcmp((a), (b)) != 0) {                                                               \
            fprintf(stderr, "  FAIL %s:%d: '%s' != '%s'\n", __FILE__, __LINE__, (a), (b));         \
            check_fail++;                                                                          \
        }                                                                                          \
    } while (0)
#define TEST_SUMMARY()                                                                             \
    fprintf(stderr, "%s: %d checks, %d failures\n", __FILE__, check_count, check_fail)

static oc_arena arena;

static oc_json *load_json(const char *path) {
    char *data = NULL;
    size_t len = 0;
    oc_status st;
    oc_status_clear(&st);
    if (oc_read_file(path, &data, &len, &st) != OC_OK)
        return NULL;
    oc_json *j = oc_json_parse(&arena, data, len, &st);
    free(data);
    return j;
}

static size_t count_findings(const oc_json *report, const char *rule) {
    const oc_json *findings = oc_json_get(report, "findings");
    if (!findings)
        return 0;
    size_t hits = 0;
    for (size_t i = 0; i < oc_json_size(findings); i++) {
        if (strcmp(oc_json_as_str(oc_json_get(findings->items[i], "rule"), ""), rule) == 0)
            hits++;
    }
    return hits;
}

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

/* SPRINT-18 portability: the committed coverage fixtures are TEMPLATES
 * -- the generating machine's absolute repo path used to be baked into
 * the bytes, which made the suites unrunnable on any other checkout
 * (GitHub runners, a user clone). The V8 adapter reads the url path off
 * disk for offset-to-line mapping (an unreadable url is a hard error by
 * contract), so each consumer substitutes the @@OC_REPO_ROOT@@
 * placeholder with THIS machine's OC_REPO_ROOT into a /tmp copy before
 * loading. The fixture template stays machine-independent and the
 * adapter contract (absolute file:// url, file must exist) is exercised
 * unchanged. Returns 0 on any failure; callers CHECK it. */
static int fixture_subst(const char *tpl_path, const char *out_path) {
    char *tpl = NULL;
    size_t tlen = 0;
    oc_status st;
    oc_status_clear(&st);
    if (oc_read_file(tpl_path, &tpl, &tlen, &st) != OC_OK)
        return 0;
    const char *ph = "@@OC_REPO_ROOT@@";
    size_t phl = strlen(ph), rl = strlen(OC_REPO_ROOT);
    size_t n = 0;
    for (const char *p = tpl; (p = strstr(p, ph)) != NULL; p += phl)
        n++;
    long delta = (long)rl - (long)phl;
    long olen = (long)tlen + delta * (long)n;
    char *out = olen > 0 ? malloc((size_t)olen) : NULL;
    int ok = 0;
    if (out) {
        size_t w = 0;
        const char *p = tpl, *q;
        for (;;) {
            q = strstr(p, ph);
            if (!q) {
                size_t rest = tlen - (size_t)(p - tpl);
                memcpy(out + w, p, rest);
                w += rest;
                break;
            }
            memcpy(out + w, p, (size_t)(q - p));
            w += (size_t)(q - p);
            memcpy(out + w, OC_REPO_ROOT, rl);
            w += rl;
            p = q + phl;
        }
        FILE *f = fopen(out_path, "wb");
        if (f) {
            ok = fwrite(out, 1, w, f) == w;
            ok = (fclose(f) == 0) && ok;
        }
        free(out);
    }
    free(tpl);
    return ok;
}

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

/* --------------------------------------------------------- version cmp */

static void test_version_cmp(void) {
    CHECK(oc_version_cmp("1.0.0", "1.0.0") == 0);
    CHECK(oc_version_cmp("1.2.3", "1.2.4") < 0);
    CHECK(oc_version_cmp("1.10.0", "1.9.0") > 0);
    CHECK(oc_version_cmp("2.0.0", "2.0.0-beta") > 0); /* prerelease sorts first */
    CHECK(oc_version_cmp("1.0", "1.0.0") == 0);       /* missing segment = 0 */
    CHECK(oc_version_cmp("2.20.0", "2.20.1") < 0);
}

/* ------------------------------------------------------------- OSV ---- */

static void test_osv(void) {
    const char *snapshot = OC_REPO_ROOT "/tests/fixtures/supply/osv_snapshot.json";
    oc_osv_snapshot s;
    oc_osv_init(&s);
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_osv_load(&s, snapshot, &arena, &st) == OC_OK);
    CHECK_STR_EQ(s.snapshot_date, "2026-11-19");
    CHECK(s.n_records == 3);

    /* hit inside the range */
    CHECK(oc_osv_match(&s, "left-pad", "1.3.0", OC_ECO_NPM) == 1);
    CHECK(oc_osv_match(&s, "requests", "2.20.0", OC_ECO_PYPI) == 1);
    CHECK(oc_osv_match(&s, "django", "3.2.0", OC_ECO_PYPI) == 1);
    /* fixed boundary: not vulnerable */
    CHECK(oc_osv_match(&s, "left-pad", "1.3.1", OC_ECO_NPM) == 0);
    /* below the introduced version */
    CHECK(oc_osv_match(&s, "left-pad", "1.1.9", OC_ECO_NPM) == 0);
    /* unknown name */
    CHECK(oc_osv_match(&s, "safe-pkg", "1.0.0", OC_ECO_PYPI) == 0);
    /* known name, no version: honestly unmatchable */
    CHECK(oc_osv_match(&s, "left-pad", NULL, OC_ECO_NPM) == 2);
    /* ecosystem mismatch */
    CHECK(oc_osv_match(&s, "left-pad", "1.3.0", OC_ECO_PYPI) == 0);
    oc_osv_destroy(&s);
}

/* --------------------------------------------------------- manifests -- */

static void test_manifests(void) {
    const char *dir = OC_REPO_ROOT "/tests/fixtures/supply";
    char p_json[512], p_req[512], p_pyproject[512];
    snprintf(p_json, sizeof(p_json), "%s/package.json", dir);
    snprintf(p_req, sizeof(p_req), "%s/requirements.txt", dir);
    snprintf(p_pyproject, sizeof(p_pyproject), "%s/pyproject.toml", dir);
    const char *paths[] = {p_json, p_req, p_pyproject};

    oc_manifest_vec mv;
    oc_manifests_init(&mv);
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_supply_load_manifests(&mv, paths, 3, &arena, &st) == OC_OK);
    CHECK(mv.n == 3);

    oc_manifest *pkg = NULL, *req = NULL, *pyproject = NULL;
    for (size_t i = 0; i < mv.n; i++) {
        if (mv.items[i].kind == OC_MANIFEST_PACKAGE_JSON)
            pkg = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_REQUIREMENTS)
            req = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_PYPROJECT)
            pyproject = &mv.items[i];
    }
    CHECK(pkg && req && pyproject);
    CHECK(pkg->eco == OC_ECO_NPM);
    CHECK(pkg->license == NULL); /* seeded: licence missing */
    CHECK(pkg->n_deps == 2);     /* range specifiers are never stored */
    CHECK_STR_EQ(pkg->deps[0].name, "left-pad");
    CHECK_STR_EQ(pkg->deps[0].version, "1.3.0");

    CHECK(req->n_deps == 3);
    CHECK_STR_EQ(req->deps[0].name, "requests");
    CHECK(req->deps[0].pinned == 1 && req->deps[0].line == 2);
    CHECK_STR_EQ(req->deps[2].name, "click");
    CHECK(req->deps[2].pinned == 0 && req->deps[2].version == NULL);

    CHECK(pyproject->n_deps == 2);
    CHECK(pyproject->license == NULL);
    CHECK_STR_EQ(pyproject->deps[0].name, "django");
    CHECK_STR_EQ(pyproject->deps[0].version, "3.2.0");
    oc_manifests_destroy(&mv);
}

/* ----------------------------------------------------------- secrets -- */

static void test_secrets(void) {
    const char *src = "const a = \"AKIAIOSFODNN7EXAMPLE\";\n"    /* AWS: exact 16 boundary */
                      "const b = \"AKIAIOSFODNN7EXAMPLEXXX\";\n" /* 19 chars: too long */
                      "-----BEGIN RSA PRIVATE KEY-----\n"        /* PEM */
                      "const g = \"ghp_0123456789abcdefghijklmnopqrstuvwxyz\";\n" /* 36 ok */
                      "const s = \"xoxb-123456789012-abc\";\n"                    /* Slack */
                      "password = \"hunter2-but-longer\";\n"                      /* generic */
                      "clean = 1;\n"
                      /* Regression (found on the pinned eslint corpus): an
                       * UNTERMINATED opening quote must not manufacture a literal out
                       * of the rest of the line. "token's" + a long tail used to be
                       * flagged as a 12+ char quoted literal; six prose comment lines
                       * in eslint's indent.js misfired exactly this way. */
                      "// Gets the first token that the given token's indentation is dependent on\n"
                      "// If the token matches the expected indentation, don't report it.\n"
                      "token = 'short';\n" /* terminated but < 12 */
                      "move_the(token);\n" /* keyword, no quote at all */
                      /* A second real assignment with a DIFFERENT keyword: the folded
                       * fingerprint must distinguish it from the password line (the
                       * old constant fp_text made every generic assignment in a file
                       * share one identity). */
                      "api_key = \"0123456789abcdef\";\n";
    oc_finding_vec out;
    oc_findings_init(&out);
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_supply_scan_secrets("t.js", src, strlen(src), &arena, &out, &st) == OC_OK);
    size_t aws = 0, pem = 0, gh = 0, slack = 0, generic = 0;
    for (size_t i = 0; i < out.n; i++) {
        const oc_finding *f = &out.items[i];
        CHECK_STR_EQ(f->rule_id, "OC011");
        if (strstr(f->message, "AWS"))
            aws++;
        else if (strstr(f->message, "PEM"))
            pem++;
        else if (strstr(f->message, "GitHub"))
            gh++;
        else if (strstr(f->message, "Slack"))
            slack++;
        else if (strstr(f->message, "Assignment"))
            generic++;
    }
    CHECK(aws == 1); /* 17-char run fails the ==16 boundary */
    CHECK(pem == 1);
    CHECK(gh == 1);
    CHECK(slack == 1);
    CHECK(generic == 2); /* the real password line + the skipped-apostrophe line */
    CHECK(out.n == 6);
    /* The two generic findings carry DIFFERENT keywords, so the folded
     * fingerprint must distinguish them (the old constant fp_text made
     * every generic assignment in a file share one identity). */
    const oc_finding *g1 = NULL, *g2 = NULL;
    for (size_t i = 0; i < out.n; i++) {
        if (strstr(out.items[i].message, "Assignment")) {
            if (!g1)
                g1 = &out.items[i];
            else
                g2 = &out.items[i];
        }
    }
    CHECK(g1 && g2);
    CHECK(strcmp(g1->fingerprint, g2->fingerprint) != 0);
    oc_findings_destroy(&out);
}

/* --------------------------------------------------------- V8 adapter */

static void test_v8_adapter(void) {
    oc_coverage cov;
    oc_coverage_init(&cov);
    oc_status st;
    oc_status_clear(&st);
    /* substitute the templates for this machine (SPRINT-18 portability) */
    CHECK(fixture_subst(OC_REPO_ROOT "/tests/fixtures/coverage/v8.json",
                        "/tmp/oc_s5_v8_fixture.json"));
    CHECK(fixture_subst(OC_REPO_ROOT "/tests/fixtures/coverage/istanbul.json",
                        "/tmp/oc_s5_ist_fixture.json"));
    const char *v8 = "/tmp/oc_s5_v8_fixture.json";
    const char *files[1] = {v8};
    CHECK(oc_coverage_load(&cov, files, 1, &arena, &st) == OC_OK);
    CHECK(cov.files_loaded == 1);
    CHECK(cov.n_files == 1);

    /* demo.js: add() fully covered; processOrder has an uncovered block */
    size_t anchors = 0, covered = 0;
    /* function spans from the graph are not rebuilt here; check by lines:
     * add() spans lines 7-9 in examples/js/demo.js */
    CHECK(oc_coverage_lookup(&cov, "examples/js/demo.js", 7, 9, &anchors, &covered) == 1);
    CHECK(anchors == 3 && covered == 3);
    /* processOrder: lines 11-79; the express branch (lines ~60-62) is
     * uncovered -> covered < anchors */
    CHECK(oc_coverage_lookup(&cov, "examples/js/demo.js", 11, 79, &anchors, &covered) == 1);
    CHECK(anchors > 50);
    CHECK(covered < anchors);
    CHECK(covered >= anchors - 5);
    /* unmeasured file: no match */
    CHECK(oc_coverage_lookup(&cov, "examples/py/demo.py", 1, 10, &anchors, &covered) == 0);
    oc_coverage_destroy(&cov);

    /* Istanbul adapter still works through the same loader */
    oc_coverage cov2;
    oc_coverage_init(&cov2);
    const char *ist = "/tmp/oc_s5_ist_fixture.json";
    const char *ist_files[1] = {ist};
    CHECK(oc_coverage_load(&cov2, ist_files, 1, &arena, &st) == OC_OK);
    CHECK(cov2.files_loaded == 1);
    CHECK(oc_coverage_lookup(&cov2, "examples/js/demo.js", 11, 79, &anchors, &covered) == 1);
    CHECK(anchors > 0 && covered < anchors);
    oc_coverage_destroy(&cov2);
}

/* ---------------------------------------------- mutation: collection -- */

static void test_mutate_collect(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/tests/fixtures/mutation/pyproj/calc.py", OC_REPO_ROOT);
    char *src = NULL;
    size_t len = 0;
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_read_file(path, &src, &len, &st) == OC_OK);

    oc_mutant_vec mv;
    oc_mutants_init(&mv);
    CHECK(oc_mutate_collect("python", path, src, len, &arena, &mv, &st) == OC_OK);
    size_t ret_change = 0, rel_swap = 0, stmt_del = 0, arg_swap = 0, other = 0;
    int docstring_deleted = 0;
    for (size_t i = 0; i < mv.n; i++) {
        const oc_mutant *m = &mv.items[i];
        if (strcmp(m->op_id, "ret-change") == 0)
            ret_change++;
        else if (strcmp(m->op_id, "rel-swap") == 0)
            rel_swap++;
        else if (strcmp(m->op_id, "stmt-del") == 0) {
            stmt_del++;
            if (m->byte_start < 70 && m->byte_start > 0 && strstr(m->original, "Mutation demo"))
                docstring_deleted = 1;
        } else if (strcmp(m->op_id, "arg-swap") == 0)
            arg_swap++;
        else
            other++;
        CHECK(m->byte_end > m->byte_start);
        CHECK(m->byte_end <= len);
        CHECK(m->line >= 1);
    }
    CHECK(ret_change == 6); /* add, 3x clamp returns, scale, pair_sum call */
    CHECK(rel_swap == 2);   /* clamp's two comparisons */
    CHECK(stmt_del == 1);   /* scale's assignment; docstring excluded */
    CHECK(arg_swap == 1);   /* pair_sum's add(a, b) call */
    CHECK(other == 0);
    CHECK(docstring_deleted == 0); /* equivalent mutant: never collected */
    /* deterministic order: byte_start ascending */
    for (size_t i = 1; i < mv.n; i++)
        CHECK(mv.items[i - 1].byte_start <= mv.items[i].byte_start);
    oc_mutants_destroy(&mv);
    free(src);

    /* unknown grammar: hard error, never silent */
    oc_mutants_init(&mv);
    CHECK(oc_mutate_collect("cobol", "x.cob", "x", 1, &arena, &mv, &st) == OC_ERR_USAGE);
    oc_mutants_destroy(&mv);
}

/* ------------------------------------- end-to-end: real oc binary runs */

static void test_e2e_supply(void) {
    const char *bin = OC_OC_BIN;
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd %s && %s check tests/fixtures/supply --packs-dir packs "
             "--osv-snapshot tests/fixtures/supply/osv_snapshot.json "
             "> /tmp/oc_s5_supply.json 2>/dev/null",
             OC_REPO_ROOT, bin);
    CHECK(run_cmd(cmd) == 1); /* error-severity findings exist */
    oc_json *r = load_json("/tmp/oc_s5_supply.json");
    CHECK(r != NULL);
    const oc_json *meta = oc_json_get(r, "meta");
    const oc_json *supply = oc_json_get(meta, "supply");
    CHECK(supply != NULL);
    CHECK(oc_json_as_num(oc_json_get(supply, "manifests_scanned"), 0) == 3);
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(supply, "osv_snapshot_date"), "?"), "2026-11-19");
    CHECK(count_findings(r, "OC009") == 4); /* left-pad, django, requests error + click warn */
    CHECK(count_findings(r, "OC010") == 2); /* package.json + pyproject.toml */
    CHECK(count_findings(r, "OC011") == 4); /* aws, ghp, xox errors + password warn */
}

static void test_e2e_leaderboard(void) {
    const char *bin = OC_OC_BIN;
    char cmd[2048];
    /* the v8 coverage template is substituted for this machine; the CLI
     * then exercises the identical adapter contract through the report */
    CHECK(fixture_subst(OC_REPO_ROOT "/tests/fixtures/coverage/v8.json", "/tmp/oc_s5_v8_e2e.json"));
    snprintf(cmd, sizeof(cmd),
             "cd %s && %s check examples --packs-dir packs "
             "--coverage /tmp/oc_s5_v8_e2e.json "
             "--coverage tests/fixtures/coverage/pycov.json "
             "> /tmp/oc_s5_lead.json 2>/dev/null",
             OC_REPO_ROOT, bin);
    run_cmd(cmd);
    oc_json *r = load_json("/tmp/oc_s5_lead.json");
    CHECK(r != NULL);
    const oc_json *lb = oc_json_get(r, "crap_leaderboard");
    CHECK(lb != NULL);
    size_t n = oc_json_size(lb);
    CHECK(n == 4); /* all four example functions measured */
    /* ordering: crap desc; verify the formula on the first row. The
     * contract is INTEGER composition: comp^2*(100-pct)/100 (floor) + comp
     * -- the double form would differ by a fraction. */
    double prev = -1;
    for (size_t i = 0; i < n; i++) {
        const oc_json *e = lb->items[i];
        double crap = oc_json_as_num(oc_json_get(e, "crap"), -1);
        if (i > 0)
            CHECK(crap <= prev); /* leaderboard is CRAP-descending */
        prev = crap;
        double comp = oc_json_as_num(oc_json_get(e, "complexity"), 0);
        double pct = oc_json_as_num(oc_json_get(e, "coverage_pct"), 0);
        long long expect =
            (long long)comp * (long long)comp * (long long)(100 - pct) / 100 + (long long)comp;
        CHECK(crap == (double)expect);
        (void)pct;
    }
    /* top entry: process_order (comp 18, partial coverage) over
     * processOrder (comp 21, 97% covered) -- coverage-aware ranking */
    const oc_json *top = lb->items[0];
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(top, "symbol"), "?"), "process_order");
}

static void test_e2e_compat(void) {
    const char *bin = OC_OC_BIN;
    char cmd[2048];
    /* breaking: export + function removed on a synthetic version bump */
    snprintf(cmd, sizeof(cmd),
             "cd %s && %s compat --baseline tests/fixtures/compat/v1 "
             "--current tests/fixtures/compat/v2 --packs-dir packs "
             "> /tmp/oc_s5_compat1.json 2>/dev/null",
             OC_REPO_ROOT, bin);
    CHECK(run_cmd(cmd) == 1); /* CI gate exit code */
    oc_json *r = load_json("/tmp/oc_s5_compat1.json");
    CHECK(r != NULL);
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(r, "verdict"), "?"), "BREAKING");
    CHECK(oc_json_size(oc_json_get(r, "breaking")) == 2);
    /* safe: additions only */
    snprintf(cmd, sizeof(cmd),
             "cd %s && %s compat --baseline tests/fixtures/compat/v1 "
             "--current tests/fixtures/compat/v2safe --packs-dir packs "
             "> /tmp/oc_s5_compat2.json 2>/dev/null",
             OC_REPO_ROOT, bin);
    CHECK(run_cmd(cmd) == 0);
    oc_json *r2 = load_json("/tmp/oc_s5_compat2.json");
    CHECK(r2 != NULL);
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(r2, "verdict"), "?"), "SAFE");
    CHECK(oc_json_size(oc_json_get(r2, "breaking")) == 0);
}

static void test_e2e_mutate(void) {
    const char *bin = OC_OC_BIN;
    char cmd[2048];
    /* file content guard: the engine must restore the mutated file */
    char before[600], calc_path[512];
    snprintf(calc_path, sizeof(calc_path), "%s/tests/fixtures/mutation/pyproj/calc.py",
             OC_REPO_ROOT);
    snprintf(before, sizeof(before), "%s.before", calc_path);
    snprintf(cmd, sizeof(cmd), "cp %s %s", calc_path, before);
    CHECK(run_cmd(cmd) == 0);

    snprintf(cmd, sizeof(cmd),
             "cd %s/tests/fixtures/mutation/pyproj && %s mutate . "
             "--packs-dir %s/packs --test-cmd \"python3 test_calc.py\" --timeout 30 "
             "> /tmp/oc_s5_mut.json 2>/dev/null",
             OC_REPO_ROOT, bin, OC_REPO_ROOT);
    CHECK(run_cmd(cmd) == 1); /* survivors exist -> gate exit 1 */
    oc_json *r = load_json("/tmp/oc_s5_mut.json");
    CHECK(r != NULL);
    CHECK_STR_EQ(oc_json_as_str(oc_json_get(r, "schema"), "?"), "omnicheck.mutation_report/v0");
    size_t total = (size_t)oc_json_as_num(oc_json_get(r, "mutants_total"), 0);
    size_t killed = (size_t)oc_json_as_num(oc_json_get(r, "killed"), 0);
    size_t survived = (size_t)oc_json_as_num(oc_json_get(r, "survived"), 0);
    CHECK(total == 10);
    CHECK(killed == 9);
    CHECK(survived == 1);
    CHECK(oc_json_as_num(oc_json_get(r, "wall_seconds"), 9999) < 30.0);
    /* exactly one survivor and it is the designed arg-swap on pair_sum */
    const oc_json *mutants = oc_json_get(r, "mutants");
    CHECK(mutants != NULL && oc_json_size(mutants) == 10);
    size_t surv_arg_swap = 0;
    for (size_t i = 0; i < oc_json_size(mutants); i++) {
        const oc_json *m = mutants->items[i];
        if (strcmp(oc_json_as_str(oc_json_get(m, "status"), ""), "survived") == 0) {
            surv_arg_swap++;
            CHECK_STR_EQ(oc_json_as_str(oc_json_get(m, "op"), "?"), "arg-swap");
        }
    }
    CHECK(surv_arg_swap == 1);
    /* the mutated file was restored byte-identically */
    snprintf(cmd, sizeof(cmd), "cmp -s %s %s", calc_path, before);
    CHECK(run_cmd(cmd) == 0);

    /* broken suite: pre-flight fails loud, never a fake kill ratio */
    snprintf(cmd, sizeof(cmd),
             "cd %s/tests/fixtures/mutation/pyproj && %s mutate . "
             "--packs-dir %s/packs --test-cmd \"python3 %s/tests/fixtures/mutation/"
             "no_such_suite.py\" "
             "--timeout 30 >/dev/null 2>/dev/null",
             OC_REPO_ROOT, bin, OC_REPO_ROOT, OC_REPO_ROOT);
    CHECK(run_cmd(cmd) == 2); /* tool error, not a report */
    snprintf(cmd, sizeof(cmd), "rm -f %s", before);
    run_cmd(cmd);
}

/* Regression (2026-11-19): the mutation report JSON must escape ALL
 * control characters inside embedded source snippets. The hand-rolled
 * printer escaped only quote/backslash/newline, so a snippet with TABs
 * (eslint's indentation style) leaked literal 0x09 bytes into the
 * strings and strict JSON parsers rejected the whole report -- caught
 * by the mutation budget re-measure on the pinned eslint repo. Fixture:
 * tab-indented body, a tab inside the argument list, a tab AND a quote
 * inside the returned expression. The emitted report must contain no
 * raw control byte, parse cleanly, and round-trip the snippets. */
static void test_e2e_mutate_escaping(void) {
    const char *bin = OC_OC_BIN;
    char cmd[2048];
    FILE *f;

    snprintf(cmd, sizeof(cmd), "rm -rf /tmp/oc_s5_tabfix && mkdir -p /tmp/oc_s5_tabfix");
    CHECK(run_cmd(cmd) == 0);
    f = fopen("/tmp/oc_s5_tabfix/calc.py", "wb");
    CHECK(f != NULL);
    fputs("def pair_sum(a, b):\n"
          "\treturn a\t+ \"b\"\n"
          "\n"
          "def main():\n"
          "\tv = pair_sum(1,\t2)\n"
          "\tprint(v)\n"
          "\n"
          "main()\n",
          f);
    fclose(f);

    /* no-op suite: every executed mutant survives -> exit 1, full report */
    snprintf(cmd, sizeof(cmd),
             "cd /tmp/oc_s5_tabfix && %s mutate . --packs-dir %s/packs "
             "--test-cmd \"true\" --timeout 30 > /tmp/oc_s5_tab_report.json 2>/dev/null",
             bin, OC_REPO_ROOT);
    CHECK(run_cmd(cmd) == 1);

    /* 1) strict-JSON hygiene: no raw control byte anywhere in the report
     *    (structural newlines live BETWEEN fields; inside strings the
     *    escaper must have replaced them with escape sequences) */
    {
        char *data = NULL;
        size_t len = 0;
        oc_status st;
        oc_status_clear(&st);
        CHECK(oc_read_file("/tmp/oc_s5_tab_report.json", &data, &len, &st) == OC_OK);
        CHECK(data != NULL);
        CHECK(strchr(data, '\t') == NULL);
        CHECK(strchr(data, '\r') == NULL);
        free(data);
    }

    /* 2) the report parses and the snippets round-trip exactly */
    oc_json *r = load_json("/tmp/oc_s5_tab_report.json");
    CHECK(r != NULL);
    const oc_json *mutants = r ? oc_json_get(r, "mutants") : NULL;
    CHECK(mutants != NULL && oc_json_size(mutants) >= 3);
    int saw_arg_tab = 0, saw_ret_tab = 0;
    for (size_t i = 0; mutants && i < oc_json_size(mutants); i++) {
        const oc_json *m = mutants->items[i];
        const char *op = oc_json_as_str(oc_json_get(m, "op"), "?");
        const char *orig = oc_json_as_str(oc_json_get(m, "original"), "");
        if (strcmp(op, "arg-swap") == 0) {
            CHECK_STR_EQ(orig, "1,\t2");
            saw_arg_tab = 1;
        }
        if (strcmp(op, "ret-change") == 0 && strchr(orig, '\t') && strchr(orig, '"')) {
            CHECK_STR_EQ(orig, "a\t+ \"b\"");
            saw_ret_tab = 1;
        }
    }
    CHECK(saw_arg_tab == 1);
    CHECK(saw_ret_tab == 1);
    snprintf(cmd, sizeof(cmd), "rm -rf /tmp/oc_s5_tabfix");
    run_cmd(cmd);
}

int main(void) {
    oc_arena_init(&arena);
    test_version_cmp();
    test_osv();
    test_manifests();
    test_secrets();
    test_v8_adapter();
    test_mutate_collect();
    test_e2e_supply();
    test_e2e_leaderboard();
    test_e2e_compat();
    test_e2e_mutate();
    test_e2e_mutate_escaping();
    TEST_SUMMARY();
    oc_arena_destroy(&arena);
    return check_fail ? 1 : 0;
}
