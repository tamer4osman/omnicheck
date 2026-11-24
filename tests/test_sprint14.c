/* Sprint 14 (descope closures): the recorded descopes of the adopted
 * languages close, contract-first (docs/LANGUAGES.md amendments of this
 * sprint).
 *
 * Go: named function-value assignments ARE symbols (three shapes,
 * single-name, non-blank, plain `=`); build constraints are evaluated
 * under the pack-DECLARED build context (//go:build expression syntax,
 * legacy // +build lines, go:build wins; unknown tags false; malformed
 * expressions over-report -- the file stays analyzed).
 * Java: abstract/interface methods ARE symbols (complexity 1, span =
 * the declaration); build.gradle(.kts) bounded dependency reader (exact
 * triplet literals + the group/name/version map form; platform() BOMs
 * skipped; dynamic versions unpinned); package-mate OC005 marking.
 * C#: property/event accessors ARE symbols (`Declaring.get` names,
 * complexity over the accessor body; auto-properties stay non-symbols);
 * packages.config + packages.lock.json NuGet tiers (exact pins;
 * lockfiles use the RESOLVED version, never the requested range);
 * OpenCover XML coverage adapter (root <CoverageSession>, inclusive
 * sl..el anchors, covered = vc > 0, fileid join); namespace-mate OC005
 * marking.
 */
#include "oc/buildtag.h"
#include "oc/common.h"
#include "oc/coverage.h"
#include "oc/graph.h"
#include "oc/parser.h"
#include "oc/supply.h"
#include "oc/surface.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

/* --- helpers ----------------------------------------------------------- */

static size_t count_symbols_named(const oc_graph *g, const char *name) {
    size_t n = 0;
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FUNCTION && strcmp(g->nodes[i].name, name) == 0)
            n++;
    }
    return n;
}

static const oc_node *symbol_named(const oc_graph *g, const char *name) {
    for (size_t i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].kind == OC_NODE_FUNCTION && strcmp(g->nodes[i].name, name) == 0)
            return &g->nodes[i];
    }
    return NULL;
}

static void parse_one(oc_parser *p, oc_graph *g, const char *path, const char *grammar,
                      const char *src) {
    oc_parser_input in;
    memset(&in, 0, sizeof(in));
    oc_parse_out out;
    memset(&out, 0, sizeof(out));
    in.path = path;
    in.source = src;
    in.len = strlen(src);
    in.grammar = grammar;
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_parser_parse_file(p, &in, g, &out, &st) == OC_OK);
    CHECK(out.diag.had_error == 0);
}

/* --- unit: Go build-constraint evaluation ------------------------------ */

static int excl_ctx(const char *src, const char *goos, const char *goarch, const char *compiler,
                    int cgo, const char *const *tags, size_t n_tags) {
    return oc_go_buildtag_excluded(src, strlen(src), goos, goarch, compiler, cgo, tags, n_tags);
}

static void unit_buildtag(void) {
    const char *extra[1] = {"mytag"};

    /* go:build: the declared GOOS includes, foreign GOOS excludes */
    CHECK(excl_ctx("//go:build linux\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    CHECK(excl_ctx("//go:build windows\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    /* negation */
    CHECK(excl_ctx("//go:build !windows\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    CHECK(excl_ctx("//go:build !linux\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    /* && / || / parens */
    CHECK(excl_ctx("//go:build (linux && amd64) || darwin\n\npackage p\n", "linux", "amd64", "gc",
                   0, NULL, 0) == 0);
    CHECK(excl_ctx("//go:build (linux && arm64) || darwin\n\npackage p\n", "linux", "amd64", "gc",
                   0, NULL, 0) == 1);
    /* unix derives from the declared GOOS */
    CHECK(excl_ctx("//go:build unix\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    CHECK(excl_ctx("//go:build unix\n\npackage p\n", "windows", "amd64", "gc", 0, NULL, 0) == 1);
    /* cgo is the declared bit */
    CHECK(excl_ctx("//go:build cgo\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    CHECK(excl_ctx("//go:build cgo\n\npackage p\n", "linux", "amd64", "gc", 1, NULL, 0) == 0);
    /* unknown tags (release tags, ignore, custom) are FALSE */
    CHECK(excl_ctx("//go:build go1.18\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    CHECK(excl_ctx("//go:build ignore\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    /* declared extra tags compare literally */
    CHECK(excl_ctx("//go:build mytag\n\npackage p\n", "linux", "amd64", "gc", 0, extra, 1) == 0);
    /* the compiler tag compares against the declared compiler */
    CHECK(excl_ctx("//go:build gc\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    CHECK(excl_ctx("//go:build gccgo\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    /* legacy // +build lines: space = OR, comma = AND, ! negates */
    CHECK(excl_ctx("// +build linux\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    CHECK(excl_ctx("// +build windows\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    CHECK(excl_ctx("// +build !linux\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
    CHECK(excl_ctx("// +build linux,amd64\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) ==
          0);
    CHECK(excl_ctx("// +build linux,arm64\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) ==
          1);
    /* two legacy lines are OR'ed */
    CHECK(excl_ctx("// +build windows\n// +build linux\n\npackage p\n", "linux", "amd64", "gc", 0,
                   NULL, 0) == 0);
    /* go:build wins over +build when both are present */
    CHECK(excl_ctx("//go:build windows\n\n// +build linux\n\npackage p\n", "linux", "amd64", "gc",
                   0, NULL, 0) == 1);
    CHECK(excl_ctx("//go:build linux\n\n// +build windows\n\npackage p\n", "linux", "amd64", "gc",
                   0, NULL, 0) == 0);
    /* a malformed expression is NO constraint: analyze (over-report) */
    CHECK(excl_ctx("//go:build ((linux\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    /* the header region ends at the first non-comment line: a constraint
     * AFTER the package clause is not one */
    CHECK(excl_ctx("package p\n\n//go:build windows\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    /* a block comment ends the header region: later directives are not
     * seen (the honest over-report direction) */
    CHECK(excl_ctx("/* doc */\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    CHECK(excl_ctx("/* doc */\n//go:build windows\n\npackage p\n", "linux", "amd64", "gc", 0, NULL,
                   0) == 0);
    /* no constraint: analyze */
    CHECK(excl_ctx("package p\n\nfunc F() {}\n", "linux", "amd64", "gc", 0, NULL, 0) == 0);
    /* leading whitespace before the directive is tolerated */
    CHECK(excl_ctx("  //go:build windows\n\npackage p\n", "linux", "amd64", "gc", 0, NULL, 0) == 1);
}

/* --- unit: Go named function-value assignments -------------------------- */

static void unit_go_literals(void) {
    static const char src[] = "package pkg\n"
                              "\n"
                              "var F1 = func() {\n"
                              "\tif true {\n"
                              "\t}\n"
                              "}\n"
                              "\n"
                              "var (\n"
                              "\tF4 = func() {\n"
                              "\t\tfor i := 0; i < 1; i++ {\n"
                              "\t\t}\n"
                              "\t}\n"
                              ")\n"
                              "\n"
                              "var f3 func()\n"
                              "\n"
                              "func outer() {\n"
                              "\tf2 := func(x int) int {\n"
                              "\t\tif x > 0 {\n"
                              "\t\t\treturn 1\n"
                              "\t\t}\n"
                              "\t\treturn 0\n"
                              "\t}\n"
                              "\t_ = f2\n"
                              "\tf3 = func() {\n"
                              "\t}\n"
                              "\t_ = func() {\n"
                              "\t\tif true {\n"
                              "\t\t}\n"
                              "\t}\n"
                              "\tgo func() {\n"
                              "\t}()\n"
                              "}\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        parse_one(p, &g, "unit/lit.go", "go", src);
        /* the three promoted shapes: :=, plain =, var spec (plain and
         * grouped) */
        CHECK(count_symbols_named(&g, "F1") == 1);
        CHECK(count_symbols_named(&g, "F4") == 1);
        CHECK(count_symbols_named(&g, "f2") == 1);
        CHECK(count_symbols_named(&g, "f3") == 1);
        CHECK(count_symbols_named(&g, "outer") == 1);
        /* the anonymous literal and the go statement are NOT promoted;
         * the blank assignment is not a name */
        CHECK(count_symbols_named(&g, "_") == 0);
        /* complexity: F1 = 1 + 1 if; F4 = 1 + 1 for; f2 = 1 + 1 if;
         * f3 = 1 (no decisions); outer = 1 + the inclusive inward count
         * of f2's if + the anonymous literal's if = 1 + 1 + 1 = 3 */
        const oc_node *s = symbol_named(&g, "F1");
        CHECK(s && s->complexity == 2);
        s = symbol_named(&g, "F4");
        CHECK(s && s->complexity == 2);
        s = symbol_named(&g, "f2");
        CHECK(s && s->complexity == 2);
        s = symbol_named(&g, "f3");
        CHECK(s && s->complexity == 1);
        s = symbol_named(&g, "outer");
        CHECK(s && s->complexity == 3);
        /* the span of a promoted symbol is the whole assignment */
        s = symbol_named(&g, "f2");
        CHECK(s && s->def_end_line >= s->def_start_line &&
              s->def_end_line - s->def_start_line >= 3);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: Go build-tag exclusion through the scan pipeline ------------- */

static void unit_buildtag_scan(void) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "cd %s/tests/fixtures_sprint14/gotags && %s check . --format json --fail-on error "
             "--packs-dir %s/packs 2>/dev/null",
             OC_REPO_ROOT, OC_OC_BIN, OC_REPO_ROOT);
    FILE *fp = popen(cmd, "r");
    CHECK(fp != NULL);
    if (fp) {
        char buf[16384];
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        int rc = pclose(fp);
        CHECK(WEXITSTATUS(rc) == 0);
        /* the windows-tagged file leaves the analyzed set (its TODO never
         * fires) and the exclusion is counted in the meta, never silent;
         * the linux-tagged file stays analyzed (its TODO does fire) */
        CHECK(strstr(buf, "\"files_excluded_build_tag\":1") != NULL);
        CHECK(strstr(buf, "only_linux.go") != NULL);
        CHECK(strstr(buf, "only_windows.go") == NULL);
    }
}

/* --- unit: Java abstract and interface methods -------------------------- */

static void unit_java_bodyless(void) {
    static const char src[] = "package p;\n"
                              "\n"
                              "public interface I {\n"
                              "    void doIt();\n"
                              "    int compute(String s);\n"
                              "    default int x() {\n"
                              "        return 1;\n"
                              "    }\n"
                              "}\n"
                              "\n"
                              "abstract class A {\n"
                              "    public abstract void run();\n"
                              "    public void real() {\n"
                              "        int y = 1;\n"
                              "    }\n"
                              "}\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        parse_one(p, &g, "unit/Body.java", "java", src);
        /* the bodyless declarations are symbols with complexity 1 */
        CHECK(count_symbols_named(&g, "doIt") == 1);
        CHECK(count_symbols_named(&g, "compute") == 1);
        CHECK(count_symbols_named(&g, "run") == 1);
        /* the two bodyful methods exist exactly once (no duplicate from
         * the bodyless query) */
        CHECK(count_symbols_named(&g, "x") == 1);
        CHECK(count_symbols_named(&g, "real") == 1);
        const oc_node *s = symbol_named(&g, "doIt");
        CHECK(s && s->complexity == 1);
        CHECK(s && s->def_start_line == s->def_end_line); /* the one-line declaration */
        s = symbol_named(&g, "real");
        CHECK(s && s->complexity == 1);
        CHECK(s && s->def_end_line > s->def_start_line);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: C# accessors are symbols ------------------------------------- */

static void unit_csharp_accessors(void) {
    static const char src[] = "namespace NS\n"
                              "{\n"
                              "public class C\n"
                              "{\n"
                              "    private int _v;\n"
                              "    public int P\n"
                              "    {\n"
                              "        get\n"
                              "        {\n"
                              "            if (_v > 0)\n"
                              "            {\n"
                              "                return 1;\n"
                              "            }\n"
                              "            return 0;\n"
                              "        }\n"
                              "        set { _v = value; }\n"
                              "    }\n"
                              "    public int Q => 1;\n"
                              "    public string Auto { get; set; }\n"
                              "    public event System.EventHandler E\n"
                              "    {\n"
                              "        add { _v++; }\n"
                              "        remove { _v--; }\n"
                              "    }\n"
                              "}\n"
                              "}\n";

    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);
    oc_parser *p = oc_parser_new(&a, &st);
    CHECK(p != NULL);
    if (p) {
        parse_one(p, &g, "unit/Acc.cs", "csharp", src);
        /* block accessors: P.get (with the if), P.set */
        const oc_node *s = symbol_named(&g, "P.get");
        CHECK(s && s->complexity == 2); /* 1 + the if */
        s = symbol_named(&g, "P.set");
        CHECK(s && s->complexity == 1);
        /* the arrow-expression-bodied property Q has no accessor_list, so
         * no accessor symbol */
        CHECK(count_symbols_named(&g, "Q.get") == 0);
        /* auto-property accessors have no body: not symbols */
        CHECK(count_symbols_named(&g, "Auto.get") == 0);
        CHECK(count_symbols_named(&g, "Auto.set") == 0);
        /* event accessors */
        s = symbol_named(&g, "E.add");
        CHECK(s && s->complexity == 1);
        s = symbol_named(&g, "E.remove");
        CHECK(s && s->complexity == 1);
    }
    oc_parser_free(p);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: gradle + legacy NuGet manifests ------------------------------ */

static void unit_supply_manifests(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);

    const char *files[3];
    static char p1[512], p2[512], p3[512];
    snprintf(p1, sizeof(p1), "%s/tests/fixtures_sprint14/gradle/build.gradle", OC_REPO_ROOT);
    snprintf(p2, sizeof(p2), "%s/tests/fixtures_sprint14/nuget/packages.config", OC_REPO_ROOT);
    snprintf(p3, sizeof(p3), "%s/tests/fixtures_sprint14/nuget/packages.lock.json", OC_REPO_ROOT);
    files[0] = p1;
    files[1] = p2;
    files[2] = p3;

    oc_manifest_vec mv;
    oc_manifests_init(&mv);
    CHECK(oc_supply_load_manifests(&mv, files, 3, &a, &st) == OC_OK);
    CHECK(mv.n == 3);

    const oc_manifest *gradle = NULL, *cfg = NULL, *lock = NULL;
    for (size_t i = 0; i < mv.n; i++) {
        if (mv.items[i].kind == OC_MANIFEST_GRADLE)
            gradle = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_PKGS_CONFIG)
            cfg = &mv.items[i];
        else if (mv.items[i].kind == OC_MANIFEST_PKGS_LOCK)
            lock = &mv.items[i];
    }
    CHECK(gradle && gradle->eco == OC_ECO_MAVEN);
    CHECK(cfg && cfg->eco == OC_ECO_NUGET);
    CHECK(lock && lock->eco == OC_ECO_NUGET);

    if (gradle) {
        /* guava (triplet pin), junit (triplet pin), jackson-databind
         * (double-quoted triplet pin), dynamic (unpinned), the ${property}
         * coordinate (unpinned), the map-form mapdb (pin); the platform()
         * BOM is skipped */
        CHECK(gradle->n_deps == 6);
        int saw_guava = 0, saw_junit = 0, saw_mapper = 0, saw_dynamic = 0, saw_prop = 0,
            saw_mapdb = 0, saw_bom = 0;
        for (size_t i = 0; i < gradle->n_deps; i++) {
            const oc_dep *d = &gradle->deps[i];
            if (strcmp(d->name, "com.google.guava:guava") == 0 && d->pinned &&
                strcmp(d->version, "32.1.2-jre") == 0)
                saw_guava = 1;
            if (strcmp(d->name, "junit:junit") == 0 && d->pinned &&
                strcmp(d->version, "4.13.2") == 0)
                saw_junit = 1;
            if (strcmp(d->name, "com.fasterxml.jackson.core:jackson-databind") == 0 && d->pinned &&
                strcmp(d->version, "2.15.0") == 0)
                saw_mapper = 1;
            if (strcmp(d->name, "org.example:dynamic") == 0 && !d->pinned && d->version == NULL)
                saw_dynamic = 1;
            if (strstr(d->name, "property-artifact") != NULL && !d->pinned && d->version == NULL)
                saw_prop = 1;
            if (strcmp(d->name, "org.mapdb:mapdb") == 0 && d->pinned &&
                strcmp(d->version, "3.0.8") == 0)
                saw_mapdb = 1;
            if (strstr(d->name, "bom") != NULL)
                saw_bom = 1;
        }
        CHECK(saw_guava);
        CHECK(saw_junit);
        CHECK(saw_mapper);
        CHECK(saw_dynamic);
        CHECK(saw_prop);
        CHECK(saw_mapdb);
        CHECK(!saw_bom);
    }
    if (cfg) {
        CHECK(cfg->n_deps == 3);
        int saw_newtonsoft = 0, saw_ranged = 0, saw_noversion = 0;
        for (size_t i = 0; i < cfg->n_deps; i++) {
            const oc_dep *d = &cfg->deps[i];
            if (strcmp(d->name, "Newtonsoft.Json") == 0 && d->pinned &&
                strcmp(d->version, "13.0.3") == 0)
                saw_newtonsoft = 1;
            if (strcmp(d->name, "Ranged.Package") == 0 && !d->pinned)
                saw_ranged = 1;
            if (strcmp(d->name, "NoVersion.Package") == 0 && !d->pinned)
                saw_noversion = 1;
        }
        CHECK(saw_newtonsoft);
        CHECK(saw_ranged);
        CHECK(saw_noversion);
    }
    if (lock) {
        CHECK(lock->n_deps == 3);
        int saw_direct = 0, saw_transitive = 0, saw_floating = 0;
        for (size_t i = 0; i < lock->n_deps; i++) {
            const oc_dep *d = &lock->deps[i];
            /* the RESOLVED version is the pin, never the requested range */
            if (strcmp(d->name, "Direct.Package") == 0 && d->pinned &&
                strcmp(d->version, "2.0.0") == 0)
                saw_direct = 1;
            if (strcmp(d->name, "Transitive.Package") == 0 && d->pinned &&
                strcmp(d->version, "1.1.0") == 0)
                saw_transitive = 1;
            if (strcmp(d->name, "Floating.Package") == 0 && d->pinned &&
                strcmp(d->version, "1.0.0") == 0)
                saw_floating = 1;
        }
        CHECK(saw_direct);
        CHECK(saw_transitive);
        CHECK(saw_floating);
    }
    oc_manifests_destroy(&mv);
    oc_arena_destroy(&a);
}

/* --- unit: OpenCover XML coverage --------------------------------------- */

static void unit_opencover(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);
    oc_coverage cov;
    oc_coverage_init(&cov);
    char path[512];
    snprintf(path, sizeof(path), "%s/tests/fixtures_sprint14/opencover/cover.xml", OC_REPO_ROOT);
    const char *paths[1] = {path};
    CHECK(oc_coverage_load(&cov, paths, 1, &a, &st) == OC_OK);
    /* two <File> elements loaded; only the one with records joins the
     * entry set */
    CHECK(cov.files_loaded == 2);
    CHECK(cov.n_files == 1);
    if (cov.n_files == 1) {
        const oc_cov_file *e = &cov.files[0];
        CHECK(strstr(e->path, "Acc.cs") != NULL);
        /* anchors are the INCLUSIVE sl..el spans: 3,4,5 + 6 + 9,10 = 6
         * lines; the unknown-fileid record (sl 20) joins nothing */
        CHECK(e->n_anchor == 6);
        CHECK(e->n_covered == 5); /* vc > 0: 3,4,5,9,10 */
    }
    oc_coverage_destroy(&cov);
    oc_arena_destroy(&a);
}

/* --- unit: package-mate and namespace-mate marking ---------------------- */

static void unit_mate_marking(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_status st;
    oc_status_clear(&st);

    /* Java: two package-mates in the same directory; a third file alone
     * in another directory (its export has NO mate). C#: two files
     * declaring the SAME dotted namespace, one declaring a namespace
     * alone. */
    oc_graph_add_file(&g, "proj/store/A.java");
    oc_graph_add_file(&g, "proj/store/B.java");
    oc_graph_add_file(&g, "proj/lonely/C.java");
    oc_graph_add_file(&g, "proj/ns/X.cs");
    oc_graph_add_file(&g, "proj/other/Y.cs");
    oc_graph_add_file(&g, "proj/ns/Z.cs");

    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    oc_bind_vec binds;
    oc_surface_binds_init(&binds);
    oc_hint_vec hints;
    oc_surface_hints_init(&hints);

    CHECK(oc_surface_export_push(&exports, &a, "proj/store/A.java", "A", 1, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/store/B.java", "B", 1, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/lonely/C.java", "C", 1, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/ns/X.cs", "X", 1, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/other/Y.cs", "Y", 1, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/ns/Z.cs", "Z", 1, 0));

    CHECK(oc_surface_hint_push_namespace(&hints, &a, "proj/ns/X.cs", "My.NS"));
    CHECK(oc_surface_hint_push_namespace(&hints, &a, "proj/other/Y.cs", "My.NS"));
    CHECK(oc_surface_hint_push_namespace(&hints, &a, "proj/ns/Z.cs", "Alone.NS"));

    oc_surface *s = oc_surface_build(&a, &g, &binds, &exports, &hints, NULL, 0, NULL, &st);
    CHECK(s != NULL);
    if (s) {
        for (size_t i = 0; i < exports.n; i++) {
            const oc_export_rec *e = &exports.items[i];
            if (strcmp(e->name, "A") == 0 || strcmp(e->name, "B") == 0)
                CHECK(e->used == 1); /* a package-mate exists */
            if (strcmp(e->name, "C") == 0)
                CHECK(e->used == 0); /* alone in its directory */
            if (strcmp(e->name, "X") == 0 || strcmp(e->name, "Y") == 0)
                CHECK(e->used == 1); /* a namespace-mate file exists */
            if (strcmp(e->name, "Z") == 0)
                CHECK(e->used == 0); /* its namespace has one file only */
        }
    }

    oc_surface_exports_destroy(&exports);
    oc_surface_binds_destroy(&binds);
    oc_surface_hints_destroy(&hints);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

int main(void) {
    unit_buildtag();
    unit_go_literals();
    unit_buildtag_scan();
    unit_java_bodyless();
    unit_csharp_accessors();
    unit_supply_manifests();
    unit_opencover();
    unit_mate_marking();
    TEST_SUMMARY();
}
