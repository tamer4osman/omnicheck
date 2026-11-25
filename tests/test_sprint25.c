/* Sprint 25 (the defect-fix verification wave): every defect found by
 * the independent nodejs/node audit, fixed in source and pinned here by
 * regression.
 *
 *   N-1  the CLI path array is dynamically grown: 300 explicit path
 *        arguments are ALL analyzed (the old fixed 256-slot array
 *        silently dropped the rest, with no diagnostic at all);
 *   N-2  a corrupt manifest is a RECORDED skip (named in the report
 *        meta with its reason), never a scan veto: the run completes
 *        and every other file is still analyzed;
 *   N-3  the module surface sees CommonJS exports -- object-literal
 *        keys of `module.exports = {...}`, one-level member writes
 *        (`exports.x = ...`, `module.exports.x = ...`), Object.assign
 *        export merges, and the honest "default" pseudo-name for
 *        opaque wholesale reassignments -- so the compat gate reports
 *        removed-export / removed-function BREAKING on CommonJS trees
 *        (the v25.0.0 -> HEAD lib/ gate previously reported SAFE while
 *        functions were disappearing);
 *   N-4  triage rank coverage is complete: the client GROWS its read
 *        buffer past 64 KiB (old: silent truncation -> parse failure ->
 *        zero ranks) and the provider emits EVERY ranked entry (old:
 *        2000-entry / 58 KiB emit cap);
 *   N-5  the Sprint-24 Scala fixtures are generated sources -- the
 *        build regenerates them when absent (CMake wiring); this file
 *        pins the sentinel fixture's presence.
 *
 * Honesty boundaries pinned alongside the fixes: computed keys, numeric
 * keys, spreads (except the wholesale-change pseudo-name), nested
 * member chains (module.exports.a.b) and foreign targets (foo.exports)
 * record NOTHING.
 */
#include "oc/compat.h"
#include "oc/common.h"
#include "oc/finding.h"
#include "oc/json.h"
#include "oc/parser.h"
#include "oc/supply.h"
#include "oc/surface.h"
#include "oc/triage.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define STR_EQ(a, b) ((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0)

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

static oc_arena arena;

/* --- small fs + process helpers ---------------------------------------- */

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static void mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

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

/* Runs `oc <args...>` with cwd=root, capturing stdout to out_path and
 * returning the process exit code (the CI-gate semantics). The buffer
 * carries up to a few thousand path arguments (the N-1 case). */
static int run_oc_to(const char *root, const char *args, const char *out_path) {
    static char cmd[16384];
    snprintf(cmd, sizeof(cmd), "cd %s && %s %s --packs-dir %s/packs > %s 2>/dev/null", root,
             OC_OC_BIN, args, OC_REPO_ROOT, out_path);
    int st = system(cmd);
    return st == -1 ? -1 : WEXITSTATUS(st);
}

static size_t exports_count_named(const oc_export_vec *v, const char *name) {
    size_t n = 0;
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->items[i].name, name) == 0)
            n++;
    return n;
}

/* --- unit: N-3 surface extraction, the honest CommonJS channel ---------- */

static void unit_cjs_surface_extraction(void) {
    static const char src[] =
        "'use strict';\n"
        "const util = require('util');\n"
        "const merged1 = 9;\n"
        "function alpha() { return 1; }\n"
        "function gone() { return 2; }\n"
        "const beta = 3;\n"
        "module.exports = {\n"
        "  alpha,\n"
        "  beta: beta,\n"
        "  'quoted-key': 1,\n"
        "  delta() { return 4; },\n"
        "  gone,\n"
        "};\n"
        "module.exports.extra = function extra() {};\n"
        "Object.assign(module.exports, { merged1, merged2: 2, 'm3': 3 });\n"
        "exports.eps = function eps() {};\n"
        "module.exports.a.b = 1; /* nested chain: NOT an export name */\n"
        "foo.exports.bar = 2;    /* foreign target: NOT an export name */\n"
        "module.exports = module.exports; /* opaque wholesale: 'default' */\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        oc_export_vec exports;
        oc_surface_exports_init(&exports);
        oc_parser_input in;
        memset(&in, 0, sizeof(in));
        in.path = "feature.js";
        in.source = src;
        in.len = strlen(src);
        in.grammar = "javascript";
        oc_parse_out out;
        memset(&out, 0, sizeof(out));
        out.exports = &exports;
        CHECK(oc_parser_parse_file(p, &in, &g, &out, &st) == OC_OK);
        CHECK(out.diag.had_error == 0);

        /* every honestly recordable CommonJS name is recorded exactly
         * once (the per-file dedup folds the re-opened forms) */
        CHECK(exports_count_named(&exports, "alpha") == 1);      /* shorthand */
        CHECK(exports_count_named(&exports, "beta") == 1);       /* pair key */
        CHECK(exports_count_named(&exports, "quoted-key") == 1); /* quoted key, unquoted */
        CHECK(exports_count_named(&exports, "delta") == 1);      /* method name */
        CHECK(exports_count_named(&exports, "gone") == 1);       /* shorthand */
        CHECK(exports_count_named(&exports, "extra") == 1);      /* module.exports.x = */
        CHECK(exports_count_named(&exports, "merged1") == 1);    /* Object.assign key */
        CHECK(exports_count_named(&exports, "merged2") == 1);    /* Object.assign pair */
        CHECK(exports_count_named(&exports, "m3") == 1);         /* Object.assign quoted */
        CHECK(exports_count_named(&exports, "eps") == 1);        /* exports.x = */
        CHECK(exports_count_named(&exports, "default") == 1);    /* opaque wholesale */
        /* the honesty boundaries record NOTHING */
        CHECK(exports_count_named(&exports, "b") == 0);   /* module.exports.a.b */
        CHECK(exports_count_named(&exports, "bar") == 0); /* foo.exports.bar */
        CHECK(exports_count_named(&exports, "util") == 0);
        CHECK(exports.n == 11);

        oc_surface_exports_destroy(&exports);
        oc_parser_free(p);
    }
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: N-3 compat gate on a CommonJS pair --------------------------- */

static void unit_cjs_compat_breaking(void) {
    mkdir_p("/tmp/oc_s25_cjs/v1/lib");
    mkdir_p("/tmp/oc_s25_cjs/v2/lib");
    mkdir_p("/tmp/oc_s25_cjs/v2safe/lib");
    write_file("/tmp/oc_s25_cjs/v1/lib/feature.js",
               "'use strict';\n"
               "function keep() { return 1; }\n"
               "function gone() { return 2; }\n"
               "module.exports = { keep, gone, alpha: 1 };\n");
    write_file("/tmp/oc_s25_cjs/v2/lib/feature.js",
               "'use strict';\n"
               "function keep() { return 1; }\n"
               "module.exports = { keep, alpha: 1 };\n");
    write_file("/tmp/oc_s25_cjs/v2safe/lib/feature.js",
               "'use strict';\n"
               "function keep() { return 1; }\n"
               "function gone() { return 2; }\n"
               "module.exports = { keep, gone, alpha: 1 };\n");

    /* breaking pair: the removed function and its export entry gate
     * BREAKING (exit 1) with exactly the two removal records */
    int ec = run_oc_to(OC_REPO_ROOT,
                       "compat --baseline /tmp/oc_s25_cjs/v1 --current /tmp/oc_s25_cjs/v2",
                       "/tmp/oc_s25_cjs/break.json");
    CHECK(ec == 1);
    oc_json *r = load_json("/tmp/oc_s25_cjs/break.json");
    CHECK(r != NULL);
    if (r) {
        CHECK_STR_EQ(oc_json_as_str(oc_json_get(r, "verdict"), "?"), "BREAKING");
        const oc_json *b = oc_json_get(r, "breaking");
        CHECK(oc_json_size(b) == 2);
        int removed_export = 0, removed_function = 0;
        for (size_t i = 0; i < oc_json_size(b); i++) {
            const oc_json *e = b->items[i];
            const char *kind = oc_json_as_str(oc_json_get(e, "kind"), "?");
            const char *sym = oc_json_as_str(oc_json_get(e, "symbol"), "?");
            if (strcmp(kind, "removed-export") == 0 && strcmp(sym, "gone") == 0)
                removed_export++;
            if (strcmp(kind, "removed-function") == 0 && strcmp(sym, "gone") == 0)
                removed_function++;
        }
        CHECK(removed_export == 1);
        CHECK(removed_function == 1);
        /* no false removals for the surviving names */
        for (size_t i = 0; i < oc_json_size(b); i++) {
            const char *sym = oc_json_as_str(oc_json_get(b->items[i], "symbol"), "?");
            CHECK(strcmp(sym, "keep") != 0);
            CHECK(strcmp(sym, "alpha") != 0);
        }
    }

    /* identical pair: SAFE (exit 0), zero breaking, zero informational */
    ec = run_oc_to(OC_REPO_ROOT,
                   "compat --baseline /tmp/oc_s25_cjs/v1 --current /tmp/oc_s25_cjs/v2safe",
                   "/tmp/oc_s25_cjs/safe.json");
    CHECK(ec == 0);
    oc_json *rs = load_json("/tmp/oc_s25_cjs/safe.json");
    CHECK(rs != NULL);
    if (rs) {
        CHECK_STR_EQ(oc_json_as_str(oc_json_get(rs, "verdict"), "?"), "SAFE");
        CHECK(oc_json_size(oc_json_get(rs, "breaking")) == 0);
        CHECK(oc_json_size(oc_json_get(rs, "informational")) == 0);
    }

    /* determinism: the same breaking pair twice is byte-identical */
    run_oc_to(OC_REPO_ROOT,
              "compat --baseline /tmp/oc_s25_cjs/v1 --current /tmp/oc_s25_cjs/v2",
              "/tmp/oc_s25_cjs/break2.json");
    char *d1 = NULL, *d2 = NULL;
    size_t l1 = 0, l2 = 0;
    oc_status st;
    oc_status_clear(&st);
    if (oc_read_file("/tmp/oc_s25_cjs/break.json", &d1, &l1, &st) == OC_OK &&
        oc_read_file("/tmp/oc_s25_cjs/break2.json", &d2, &l2, &st) == OC_OK)
        CHECK(l1 == l2 && memcmp(d1, d2, l1) == 0);
    free(d1);
    free(d2);
}

/* --- unit: N-1 the CLI analyzes every path argument ---------------------- */

static void unit_n1_paths_beyond_256(void) {
    mkdir_p("/tmp/oc_s25_n1");
    char path[64];
    for (int i = 0; i < 300; i++) {
        snprintf(path, sizeof(path), "/tmp/oc_s25_n1/f%03d.js", i);
        char body[64];
        snprintf(body, sizeof(body), "// plain file f%03d, nothing to find\n", i);
        write_file(path, body);
    }
    /* 300 explicit path arguments: the old fixed 256-slot array
     * silently analyzed only the first 256. */
    char args[8192];
    int off = snprintf(args, sizeof(args), "check");
    for (int i = 0; i < 300; i++) {
        snprintf(path, sizeof(path), " f%03d.js", i);
        strcat(args + off, path);
        off += (int)strlen(path);
    }
    snprintf(args + off, sizeof(args) - (size_t)off, " --format json");
    int ec = run_oc_to("/tmp/oc_s25_n1", args, "/tmp/oc_s25_n1/report.json");
    CHECK(ec == 0);
    oc_json *r = load_json("/tmp/oc_s25_n1/report.json");
    CHECK(r != NULL);
    if (r) {
        const oc_json *meta = oc_json_get(r, "meta");
        CHECK(meta != NULL);
        long analyzed = (long)oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1);
        CHECK(analyzed == 300); /* every path argument analyzed, none dropped */
    }
}

/* --- unit: N-2 a corrupt manifest is a recorded skip --------------------- */

static void unit_n2_corrupt_manifest_skip(void) {
    mkdir_p("/tmp/oc_s25_n2/sub");
    write_file("/tmp/oc_s25_n2/package.json", "{\"name\": \"corrupt\", \"version\": \"1"); /* torn */
    write_file("/tmp/oc_s25_n2/app.js", "const x = 1;\n");
    write_file("/tmp/oc_s25_n2/sub/package.json",
               "{\"name\": \"ok\", \"version\": \"1.0.0\", \"license\": \"MIT\"}\n");
    write_file("/tmp/oc_s25_n2/sub/lib.js", "const y = 2;\n");

    int ec = run_oc_to("/tmp/oc_s25_n2", "check . --format json", "/tmp/oc_s25_n2/report.json");
    /* the run COMPLETES (exit 0: no findings, licence ok) -- the corrupt
     * manifest no longer vetoes the scan */
    CHECK(ec == 0);
    oc_json *r = load_json("/tmp/oc_s25_n2/report.json");
    CHECK(r != NULL);
    if (r) {
        const oc_json *meta = oc_json_get(r, "meta");
        CHECK(meta != NULL);
        const oc_json *supply = oc_json_get(meta, "supply");
        CHECK(supply != NULL);
        long scanned = (long)oc_json_as_num(oc_json_get(supply, "manifests_scanned"), -1);
        long skipped = (long)oc_json_as_num(oc_json_get(supply, "manifests_skipped"), -1);
        CHECK(scanned == 1); /* the valid sub/package.json still parsed */
        CHECK(skipped == 1); /* the corrupt root package.json is named */
        const oc_json *sk = oc_json_get(supply, "manifest_skip");
        CHECK(oc_json_size(sk) == 1);
        if (oc_json_size(sk) == 1) {
            const char *skf = oc_json_as_str(oc_json_get(sk->items[0], "file"), "?");
            /* the root is scanned as ".": discovered paths carry the
             * './' prefix; the corrupt one is the ROOT manifest, not
             * the valid sub/package.json */
            CHECK(skf && strcmp(skf, "./package.json") == 0);
            const char *reason = oc_json_as_str(oc_json_get(sk->items[0], "reason"), "?");
            CHECK(reason && strstr(reason, "unparseable") == reason);
        }
        long analyzed = (long)oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1);
        CHECK(analyzed == 2); /* app.js + sub/lib.js were still analyzed */
    }
}

/* --- unit: N-4 client side, the response buffer grows -------------------- */

static void push_finding(oc_finding_vec *v, oc_arena *a, int i) {
    char msg[160], fp[64];
    snprintf(msg, sizeof(msg), "Function 'fn%04d' spans %d lines and needs a triage verdict", i,
             61 + i % 40);
    snprintf(fp, sizeof(fp), "fp-s25-%06d", i);
    oc_finding_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.rule_id = "OC001";
    spec.rule_name = "long-function";
    spec.severity = OC_SEV_WARN;
    spec.file = "src/big.js";
    spec.line = (size_t)(1 + i);
    spec.col = 1;
    spec.end_line = spec.line + 70;
    spec.symbol = NULL;
    spec.message = msg;
    spec.fp_text = fp;
    CHECK(oc_findings_push(v, a, &spec));
}

#ifdef OC_PROVIDER_BIN
/* The REAL provider must emit EVERY ranked entry: 2,500 items exceed
 * both old caps (the 2,000-entry emit cap AND the 64 KiB client read
 * buffer). Rank coverage must be complete, not best-first truncated. */
static void unit_n4_provider_full_emit(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_finding_vec v;
    oc_findings_init(&v);
    for (int i = 0; i < 2500; i++)
        push_finding(&v, &a, i);

    setenv("OC_OMNIDECIDE_BIN", OC_PROVIDER_BIN, 1);
    setenv("OC_OMNIDECIDE_TIMEOUT_S", "120", 1); /* the kernel ranks 2,500 prose items */
    oc_triage_outcome out;
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    CHECK(out.ranked == 1);
    CHECK(out.ranked_count == 2500);
    size_t triaged = 0;
    for (size_t i = 0; i < v.n; i++)
        if (v.items[i].triaged)
            triaged++;
    CHECK(triaged == 2500);
    unsetenv("OC_OMNIDECIDE_BIN");
    unsetenv("OC_OMNIDECIDE_TIMEOUT_S");
    oc_findings_destroy(&v);
    oc_arena_destroy(&a);
}
#endif

/* A mock provider whose response is LARGER than the old fixed 64 KiB
 * read buffer: the old client truncated it mid-JSON and fail-opened
 * with zero ranks; the new client grows and ranks everything. */
static void unit_n4_client_grows_response(void) {
    static const char MOCK[] =
        "#!/usr/bin/env python3\n"
        "import sys, json\n"
        "obj = json.loads(sys.stdin.readline())\n"
        "ranks = []\n"
        "for i, it in enumerate(obj[\"items\"]):\n"
        "    ranks.append({\"id\": it[\"id\"], \"rank\": i, \"confidence\": 0.5,\n"
        "                  \"note\": \"filler\" * 24})\n" /* ~120 B/entry */
        "sys.stdout.write(json.dumps({\"model\": \"mock-big\", \"ranks\": ranks}))\n";
    write_file("/tmp/oc_s25_bigmock.py", MOCK);
    chmod("/tmp/oc_s25_bigmock.py", 0755);

    oc_arena a;
    oc_arena_init(&a);
    oc_finding_vec v;
    oc_findings_init(&v);
    for (int i = 0; i < 600; i++) /* 600 x ~150 B response > 64 KiB */
        push_finding(&v, &a, i);

    setenv("OC_OMNIDECIDE_BIN", "/tmp/oc_s25_bigmock.py", 1);
    setenv("OC_OMNIDECIDE_TIMEOUT_S", "60", 1);
    oc_triage_outcome out;
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_triage_rank_findings(&v, &out, &st) == OC_OK);
    CHECK(out.ranked == 1);
    CHECK(out.ranked_count == 600);
    size_t triaged = 0;
    for (size_t i = 0; i < v.n; i++)
        if (v.items[i].triaged)
            triaged++;
    CHECK(triaged == 600);
    unsetenv("OC_OMNIDECIDE_BIN");
    unsetenv("OC_OMNIDECIDE_TIMEOUT_S");
    oc_findings_destroy(&v);
    oc_arena_destroy(&a);
    remove("/tmp/oc_s25_bigmock.py");
}

/* --- unit: OC002-R Rust block comments enter the comment channel --------- */

static void unit_oc002_rust_block_comments(void) {
    mkdir_p("/tmp/oc_s25_oc002r");
    write_file("/tmp/oc_s25_oc002r/crate.rs",
               "// TODO: line marker (was already seen)\n"
               "fn keep() {}\n"
               "/* FIXME: block marker at the open line */\n"
               "fn keep2() {}\n"
               "/* multi-line block with a HACK marker\n"
               "   on the continuation line */\n"
               "fn keep3() {}\n");
    int ec = run_oc_to("/tmp/oc_s25_oc002r", "check crate.rs --format json",
                       "/tmp/oc_s25_oc002r/report.json");
    CHECK(ec == 0);
    oc_json *r = load_json("/tmp/oc_s25_oc002r/report.json");
    CHECK(r != NULL);
    if (r) {
        size_t n_todo = 0, n_fixme = 0, n_hack = 0;
        const oc_json *fs = oc_json_get(r, "findings");
        CHECK(fs != NULL);
        for (size_t i = 0; fs && i < oc_json_size(fs); i++) {
            const oc_json *f = fs->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), "?"), "OC002") != 0)
                continue;
            const char *msg = oc_json_as_str(oc_json_get(f, "message"), "");
            if (strncmp(msg, "TODO", 4) == 0)
                n_todo++;
            if (strncmp(msg, "FIXME", 5) == 0)
                n_fixme++;
            if (strncmp(msg, "HACK", 4) == 0)
                n_hack++;
        }
        CHECK(n_todo == 1);  /* line comment */
        CHECK(n_fixme == 1); /* block comment (BUGFIX OC002-R) */
        CHECK(n_hack == 1);  /* multi-line block comment */
    }
}

/* --- unit: N-6 explicit file roots keep dirname-anchored resolution ------ */

static void unit_n6_file_root_include_cycle(void) {
    mkdir_p("/tmp/oc_s25_n6");
    write_file("/tmp/oc_s25_n6/engine.c",
               "#include \"engine.h\"\n"
               "#include \"drive.h\"\n"
               "int engine_tick(void) { return drive_tick(); }\n");
    write_file("/tmp/oc_s25_n6/engine.h",
               "#include \"drive.h\"\n"
               "int engine_tick(void);\n");
    write_file("/tmp/oc_s25_n6/drive.h",
               "#include \"engine.h\"\n"
               "int drive_tick(void);\n");
    /* explicit FILE arguments (the chunked-scan shape): the dirname-
     * anchored include resolution must survive -- the old N-6 bug made
     * the analysis root the file itself, so every quoted include went
     * dead and the engine.h/drive.h cycle was invisible. */
    int ec = run_oc_to("/tmp/oc_s25_n6",
                       "check engine.c engine.h drive.h --format json",
                       "/tmp/oc_s25_n6/report.json");
    CHECK(ec == 1); /* the OC003 cycle is warn severity: the CI gate trips */
    oc_json *r = load_json("/tmp/oc_s25_n6/report.json");
    CHECK(r != NULL);
    if (r) {
        CHECK(oc_json_as_num(oc_json_get(oc_json_get(r, "meta"), "files_with_parse_errors"),
                             -1) == 0);
        const oc_json *fs = oc_json_get(r, "findings");
        int cycles = 0;
        for (size_t i = 0; fs && i < oc_json_size(fs); i++) {
            const oc_json *f = fs->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), "?"), "OC003") == 0)
                cycles++;
        }
        CHECK(cycles == 1); /* the include cycle among engine.h/drive.h */
    }
}

/* --- unit: N-5 the generated Scala fixtures exist ------------------------ */

static void unit_n5_scala_fixtures_present(void) {
    struct stat sb;
    CHECK(stat(OC_REPO_ROOT "/tests/fixtures/scala/decls.scala", &sb) == 0);
    CHECK(stat(OC_REPO_ROOT "/tests/fixtures/scala_resolver/core.scala", &sb) == 0);
}

/* --- unit: N-2 legacy fail-loud contract survives for API users --------- */

static void unit_n2_legacy_fail_loud(void) {
    mkdir_p("/tmp/oc_s25_legacy");
    write_file("/tmp/oc_s25_legacy/package.json", "{ torn json");
    const char *files[1] = {"/tmp/oc_s25_legacy/package.json"};
    oc_arena a;
    oc_arena_init(&a);
    oc_manifest_vec mv;
    oc_status st;
    oc_status_clear(&st);
    /* the legacy wrapper keeps the documented fail-loud contract */
    CHECK(oc_supply_load_manifests(&mv, files, 1, &a, &st) != OC_OK);
    oc_manifests_destroy(&mv);
    /* the _ex contract records the skip and reports success */
    oc_manifest_vec mv2;
    oc_manifest_skip_vec skips;
    oc_manifest_skips_init(&skips);
    oc_status_clear(&st);
    CHECK(oc_supply_load_manifests_ex(&mv2, &skips, files, 1, &a, &st) == OC_OK);
    CHECK(mv2.n == 0);
    CHECK(skips.n == 1);
    CHECK(STR_EQ(skips.items[0].path, "/tmp/oc_s25_legacy/package.json"));
    CHECK(skips.items[0].reason && strstr(skips.items[0].reason, "unparseable") != NULL);
    oc_manifests_destroy(&mv2);
    oc_manifest_skips_destroy(&skips);
    oc_arena_destroy(&a);
}

int main(void) {
    oc_arena_init(&arena);
    unit_cjs_surface_extraction();
    unit_cjs_compat_breaking();
    unit_n1_paths_beyond_256();
    unit_n2_corrupt_manifest_skip();
    unit_n2_legacy_fail_loud();
    unit_n4_client_grows_response();
#ifdef OC_PROVIDER_BIN
    unit_n4_provider_full_emit();
#endif
    unit_n6_file_root_include_cycle();
    unit_oc002_rust_block_comments();
    unit_n5_scala_fixtures_present();
    TEST_SUMMARY();
}
