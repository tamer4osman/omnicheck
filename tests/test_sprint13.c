/* Sprint 13 (Track D wave 1): C# adopted at FULL scope --
 * docs/LANGUAGES.md C# section. Resolver v1-csharp: plain usings
 * resolve to the namespace directory A/B/C (verbatim dots-to-slashes,
 * whole-namespace fan-out, direct-children rule), static usings
 * resolve to the type file A/B/C.cs, alias usings probe in the
 * documented order (type file first, namespace directory second); no
 * project/MSBuild reading by the resolver. Surface: public top-level
 * types are export candidates (namespace and preprocessor wrappers
 * climbed, type nesting is not), static bindings mark the type,
 * namespace/alias claims mark fail-open; the parsed static-Main fact
 * and the C# 9 top-level-statements fact claim entry points.
 * Complexity: classic kinds + switch_expression_arm + catch filters +
 * conditional_expression plus the && / || / ?? operators; switch
 * sections count only with a case label. Supply: .csproj manifests
 * (NuGet, exact literal PackageReference pins; property refs, floating
 * and ranged versions are not pins) with the
 * layered licence (expression / verified PackageLicenseFile /
 * project-root licence FILE). Coverage: Coverlet Cobertura XML line
 * records (covered = hits > 0), dispatched by the report's ROOT
 * ELEMENT. */
#include "oc/common.h"
#include "oc/coverage.h"
#include "oc/graph.h"
#include "oc/imports.h"
#include "oc/json.h"
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

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1)
        return -1;
    if (rc == 0)
        return 0;
    return WEXITSTATUS(rc);
}

static int files_equal(const char *p1, const char *p2) {
    FILE *f1 = fopen(p1, "rb");
    FILE *f2 = fopen(p2, "rb");
    if (!f1 || !f2) {
        if (f1)
            fclose(f1);
        if (f2)
            fclose(f2);
        return 0;
    }
    int same = 1;
    int c1, c2;
    while (1) {
        c1 = fgetc(f1);
        c2 = fgetc(f2);
        if (c1 != c2) {
            same = 0;
            break;
        }
        if (c1 == EOF)
            break;
    }
    fclose(f1);
    fclose(f2);
    return same;
}

static int has_edge(const oc_graph *g, const char *from, const char *to) {
    oc_node_id f = oc_graph_find_file(g, from);
    oc_node_id t = oc_graph_find_file(g, to);
    if (f == OC_NODE_INVALID || t == OC_NODE_INVALID)
        return 0;
    for (size_t i = 0; i < g->n_edges; i++)
        if (g->edges[i].kind == OC_EDGE_IMPORTS && g->edges[i].from == f && g->edges[i].to == t)
            return 1;
    return 0;
}

static void spew(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

/* --- unit: resolver v1-csharp -------------------------------------------- */
static void unit_csharp_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    oc_graph_add_file(&g, "proj/MyApp/Store/Store.cs");
    oc_graph_add_file(&g, "proj/MyApp/Store/Types.cs");
    oc_graph_add_file(&g, "proj/MyApp/Store/StoreInfo.cs");
    oc_graph_add_file(&g, "proj/MyApp/Store/Inventory.cs");
    oc_graph_add_file(&g, "proj/MyApp/Store/Deep/Nested.cs");
    oc_graph_add_file(&g, "proj/MyApp/Store/res.txt");
    oc_graph_add_file(&g, "proj/MyApp/App/Program.cs");
    oc_graph_add_file(&g, "proj/MyApp/A/A.cs");
    oc_graph_add_file(&g, "proj/MyApp/B/B.cs");

    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    /* namespace claim: verbatim dots-to-slashes directory view */
    const char *dir = oc_resolve_csharp_ns_dir(cx, "MyApp.Store", &a);
    CHECK_STR_EQ(dir, "proj/MyApp/Store");
    CHECK(oc_resolve_csharp_ns_dir(cx, "MyApp.Nopkg", &a) == NULL);
    /* a subdirectory namespace IS its own directory (the claim claims
     * the namespace's own direct children, never the parent's) */
    dir = oc_resolve_csharp_ns_dir(cx, "MyApp.Store.Deep", &a);
    CHECK_STR_EQ(dir, "proj/MyApp/Store/Deep");
    /* the single-target view: lexicographically-first .cs (the sorted
     * index puts Inventory.cs ahead of Store.cs) */
    const char *t =
        oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "MyApp.Store", OC_LANG_CSHARP);
    CHECK_STR_EQ(t, "proj/MyApp/Store/Inventory.cs");

    /* type claim: the exact file probe is the ONLY mapping */
    t = oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "static MyApp.Store.Store",
                             OC_LANG_CSHARP);
    CHECK_STR_EQ(t, "proj/MyApp/Store/Store.cs");
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "static MyApp.Store.Missing",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "static System.Linq",
                               OC_LANG_CSHARP) == NULL);

    /* alias claim: the ordered probe -- the type file FIRST, then the
     * namespace directory */
    t = oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "Inv = MyApp.Store.Inventory",
                             OC_LANG_CSHARP);
    CHECK_STR_EQ(t, "proj/MyApp/Store/Inventory.cs"); /* type file hit */
    t = oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "Sub = MyApp.Store.Deep",
                             OC_LANG_CSHARP);
    CHECK_STR_EQ(t, "proj/MyApp/Store/Deep/Nested.cs"); /* directory hit */
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "G = MyApp.Missing",
                               OC_LANG_CSHARP) == NULL);

    /* guards: leading/trailing dot, empty dot-segment, path chars,
     * absolute, generics -- all stay unresolved (no path escapes) */
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", ".MyApp.Store",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "MyApp..Store",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "MyApp.Store.",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "MyApp.Store/x",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "/MyApp/Store",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "MyApp.Store<int>",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "Ext::MyApp.Store",
                               OC_LANG_CSHARP) == NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/MyApp/App/Program.cs", "", OC_LANG_CSHARP) == NULL);

    /* fan-out: one namespace record -> one edge per DIRECT-CHILD .cs
     * file (res.txt and Store/Deep/Nested.cs are not targets), and the
     * imports relation is a SET: four records naming Store
     * (namespace + global:: + static + ...) yield ONE edge. */
    oc_import_vec v;
    oc_imports_init(&v);
    CHECK(oc_imports_push(&v, &a, "proj/MyApp/App/Program.cs", "MyApp.Store", OC_LANG_CSHARP));
    CHECK(oc_imports_push(&v, &a, "proj/MyApp/App/Program.cs", "static MyApp.Store.Store",
                          OC_LANG_CSHARP));
    CHECK(oc_imports_push(&v, &a, "proj/MyApp/App/Program.cs", "Inv = MyApp.Store.Inventory",
                          OC_LANG_CSHARP));
    CHECK(oc_imports_push(&v, &a, "proj/MyApp/App/Program.cs", "Sub = MyApp.Store.Deep",
                          OC_LANG_CSHARP));
    CHECK(oc_imports_push(&v, &a, "proj/MyApp/App/Program.cs", "System.Linq", OC_LANG_CSHARP));
    CHECK(oc_imports_push(&v, &a, "proj/MyApp/B/B.cs", "MyApp.A", OC_LANG_CSHARP));
    long added = oc_imports_resolve_ex(&g, &v, &a, roots, 1);
    CHECK(added == 6); /* App->Store, App->Types, App->StoreInfo,
                        * App->Inventory, App->deep/Nested, B->A -- Store
                        * and Inventory deduped across their records,
                        * res.txt never claimed, System.Linq unresolved */
    size_t n_store_edges = 0;
    oc_node_id f = oc_graph_find_file(&g, "proj/MyApp/App/Program.cs");
    oc_node_id s = oc_graph_find_file(&g, "proj/MyApp/Store/Store.cs");
    CHECK(f != OC_NODE_INVALID && s != OC_NODE_INVALID);
    for (size_t i = 0; i < g.n_edges; i++) {
        if (g.edges[i].kind == OC_EDGE_IMPORTS && g.edges[i].from == f && g.edges[i].to == s)
            n_store_edges++;
    }
    CHECK(n_store_edges == 1); /* imports relation is a set for C# */
    CHECK(has_edge(&g, "proj/MyApp/App/Program.cs", "proj/MyApp/Store/Store.cs"));
    CHECK(has_edge(&g, "proj/MyApp/App/Program.cs", "proj/MyApp/Store/Deep/Nested.cs"));
    CHECK(has_edge(&g, "proj/MyApp/B/B.cs", "proj/MyApp/A/A.cs"));
    oc_imports_destroy(&v);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: C# module surface marking + entry facts ----------------------- */
static void unit_csharp_marking(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_graph_add_file(&g, "proj/MyApp/Store/Store.cs");
    oc_graph_add_file(&g, "proj/MyApp/Store/Types.cs");
    oc_graph_add_file(&g, "proj/MyApp/Store/Deep/Nested.cs");
    oc_graph_add_file(&g, "proj/MyApp/App/Program.cs");

    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    CHECK(oc_surface_export_push(&exports, &a, "proj/MyApp/Store/Store.cs", "Store", 2, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/MyApp/Store/Types.cs", "Types", 2, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/MyApp/Store/Deep/Nested.cs", "Nested", 2, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/MyApp/App/Program.cs", "Program", 3, 0));

    oc_bind_vec binds;
    oc_surface_binds_init(&binds);
    CHECK(oc_surface_bind_push(&binds, &a, "proj/MyApp/App/Program.cs", "MyApp.Store", NULL,
                               OC_LANG_CSHARP));
    CHECK(oc_surface_bind_push(&binds, &a, "proj/MyApp/App/Program.cs", "static MyApp.Store.Store",
                               "Store", OC_LANG_CSHARP));
    oc_hint_vec hints;
    oc_surface_hints_init(&hints);
    CHECK(oc_surface_hint_push_main(&hints, &a, "proj/MyApp/App/Program.cs"));
    CHECK(oc_surface_hint_push_top(&hints, &a, "proj/MyApp/App/Program.cs"));

    const char *roots[1] = {"proj"};
    oc_status st;
    oc_status_clear(&st);
    oc_surface *s = oc_surface_build(&a, &g, &binds, &exports, &hints, roots, 1, NULL, &st);
    CHECK(s != NULL);
    if (s) {
        CHECK(s->binds_resolved == 2);
        CHECK(exports.items[0].used == 1); /* namespace claim: fail-open */
        CHECK(exports.items[1].used == 1); /* namespace claim: fail-open */
        CHECK(exports.items[2].used == 0); /* SUBDIRECTORY: never claimed by the claim */
        CHECK(exports.items[3].used == 0); /* Program's own export: nothing binds it */
        /* main-fact and top-level-statements entry claiming */
        oc_node_id app = oc_graph_find_file(&g, "proj/MyApp/App/Program.cs");
        oc_node_id store = oc_graph_find_file(&g, "proj/MyApp/Store/Store.cs");
        CHECK(oc_surface_is_entry(s, "proj/MyApp/App/Program.cs", app));
        CHECK(!oc_surface_is_entry(s, "proj/MyApp/Store/Store.cs", store));
    }

    oc_surface_hints_destroy(&hints);
    oc_surface_binds_destroy(&binds);
    oc_surface_exports_destroy(&exports);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: .csproj manifest loader --------------------------------------- */
static void unit_csproj(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);

    run_cmd("rm -rf /tmp/omnicheck_s13_csproj && mkdir -p /tmp/omnicheck_s13_csproj");
    spew("/tmp/omnicheck_s13_csproj/App.csproj",
         "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
         "  <!-- a commented-out PackageReference must NOT be read:\n"
         "  <PackageReference Include=\"Gone.Ghost\" Version=\"9.9.9\" /> -->\n"
         "  <PropertyGroup>\n"
         "    <PackageLicenseExpression>Apache-2.0</PackageLicenseExpression>\n"
         "  </PropertyGroup>\n"
         "  <ItemGroup>\n"
         "    <PackageReference Include=\"Ok.Lib\" Version=\"1.2.3\" />\n"
         "    <PackageReference Include=\"Ranged.Lib\" Version=\"[1.0,2.0)\" />\n"
         "    <PackageReference Include=\"Prop.Lib\" Version=\"$(Lib.Version)\" />\n"
         "    <PackageReference Include=\"Float.Lib\" Version=\"1.2.*\" />\n"
         "    <PackageReference Include=\"Child.Lib\">\n"
         "      <Version>0.4.0</Version>\n"
         "    </PackageReference>\n"
         "  </ItemGroup>\n"
         "</Project>\n");

    const char *files[] = {"/tmp/omnicheck_s13_csproj/App.csproj"};
    oc_manifest_vec mv;
    CHECK(oc_supply_load_manifests(&mv, files, 1, &a, &st) == OC_OK);
    CHECK(mv.n == 1);
    if (mv.n == 1) {
        const oc_manifest *m = &mv.items[0];
        CHECK(m->kind == OC_MANIFEST_CSPROJ);
        CHECK(m->eco == OC_ECO_NUGET);
        CHECK_STR_EQ(oc_ecosystem_str(m->eco), "NuGet");
        CHECK_STR_EQ(oc_manifest_kind_str(m->kind), ".csproj");
        CHECK_STR_EQ(m->license, "Apache-2.0");
        /* one dep per <PackageReference> element; the ghost inside the
         * comment is gone; the child-element version is read */
        CHECK(m->n_deps == 5);
        if (m->n_deps == 5) {
            CHECK_STR_EQ(m->deps[0].name, "Ok.Lib");
            CHECK_STR_EQ(m->deps[0].version, "1.2.3");
            CHECK(m->deps[0].pinned);
            CHECK_STR_EQ(m->deps[1].name, "Ranged.Lib");
            CHECK(m->deps[1].version == NULL); /* range: never guessed */
            CHECK(!m->deps[1].pinned);
            CHECK_STR_EQ(m->deps[2].name, "Prop.Lib");
            CHECK(!m->deps[2].pinned); /* $(property) reference */
            CHECK_STR_EQ(m->deps[3].name, "Float.Lib");
            CHECK(!m->deps[3].pinned); /* floating 1.2.* */
            CHECK_STR_EQ(m->deps[4].name, "Child.Lib");
            CHECK_STR_EQ(m->deps[4].version, "0.4.0"); /* child-element form */
            CHECK(m->deps[4].pinned);
        }
    }
    oc_manifests_destroy(&mv);

    /* no licence declared anywhere, no licence file in the root: the
     * licence stays NULL (OC010 warns); the root-FILE convention fires
     * when a LICENSE file is among the discovered files */
    spew("/tmp/omnicheck_s13_csproj/App.csproj",
         "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
         "  <ItemGroup>\n"
         "    <PackageReference Include=\"Clean.Lib\" Version=\"1.0.0\" />\n"
         "  </ItemGroup>\n"
         "</Project>\n");
    oc_manifest_vec mv2;
    CHECK(oc_supply_load_manifests(&mv2, files, 1, &a, &st) == OC_OK);
    CHECK(mv2.n == 1);
    if (mv2.n == 1) {
        CHECK(mv2.items[0].license == NULL);
        CHECK(mv2.items[0].n_deps == 1);
    }
    oc_manifests_destroy(&mv2);

    /* with LICENSE directly inside the project root (discovered files):
     * the Go-convention file detection names it */
    spew("/tmp/omnicheck_s13_csproj/LICENSE", "MIT\n");
    const char *files2[] = {"/tmp/omnicheck_s13_csproj/App.csproj",
                            "/tmp/omnicheck_s13_csproj/LICENSE"};
    oc_manifest_vec mv3;
    CHECK(oc_supply_load_manifests(&mv3, files2, 2, &a, &st) == OC_OK);
    CHECK(mv3.n == 1);
    if (mv3.n == 1)
        CHECK_STR_EQ(mv3.items[0].license, "project-root file LICENSE");
    oc_manifests_destroy(&mv3);

    oc_status_clear(&st);
    oc_arena_destroy(&a);
}

/* --- unit: Cobertura XML adapter ----------------------------------------- */
static void unit_cobertura(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_status_clear(&st);

    run_cmd("rm -rf /tmp/omnicheck_s13_cov && mkdir -p /tmp/omnicheck_s13_cov");
    spew("/tmp/omnicheck_s13_cov/coverage.xml",
         "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
         "<coverage line-rate=\"0.5\" version=\"6.0.4\">\n"
         "  <!-- <line number=\"999\" hits=\"9\"/> must NOT be read -->\n"
         "  <packages>\n"
         "    <package name=\"app\">\n"
         "      <classes>\n"
         "        <class name=\"App.Store\" filename=\"app/store/Store.cs\">\n"
         "          <lines>\n"
         "            <line number=\"11\" hits=\"5\" branch=\"False\" />\n"
         "            <line number=\"12\" hits=\"0\" branch=\"False\" />\n"
         "            <line number=\"13\" hits=\"2\" branch=\"True\" />\n"
         "          </lines>\n"
         "        </class>\n"
         "      </classes>\n"
         "    </package>\n"
         "  </packages>\n"
         "</coverage>\n");

    oc_coverage cov;
    oc_coverage_init(&cov);
    const char *paths[] = {"/tmp/omnicheck_s13_cov/coverage.xml"};
    CHECK(oc_coverage_load(&cov, paths, 1, &a, &st) == OC_OK);
    CHECK(cov.n_files == 1);
    CHECK(cov.files_loaded == 1);

    size_t anchors = 0, covered = 0;
    /* exact path match: the class filename, verbatim */
    CHECK(oc_coverage_lookup(&cov, "app/store/Store.cs", 1, 100, &anchors, &covered));
    CHECK(anchors == 3); /* lines 11, 12, 13 carry records */
    CHECK(covered == 2); /* hits > 0 on 11 and 13; 12 is missed (hits=0) */
    /* span-restricted: the honest covered/uncovered split */
    CHECK(oc_coverage_lookup(&cov, "app/store/Store.cs", 12, 12, &anchors, &covered));
    CHECK(anchors == 1);
    CHECK(covered == 0);

    /* suffix rule: the analyzed path is the tail of the coverage path */
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_graph_add_file(&g, "store/Store.cs");
    CHECK(oc_coverage_lookup(&cov, "store/Store.cs", 1, 100, &anchors, &covered));
    CHECK(anchors == 3);

    oc_coverage_destroy(&cov);
    oc_graph_destroy(&g);

    /* malformed record fails loud (never compose silently) */
    spew("/tmp/omnicheck_s13_cov/bad.xml",
         "<?xml version=\"1.0\"?><coverage><packages><package name=\"p\">"
         "<classes><class name=\"X\" filename=\"X.cs\"><lines><line hits=\"1\"/>"
         "</lines></class></classes></package></coverage>\n");
    oc_coverage cov2;
    oc_coverage_init(&cov2);
    const char *bad[] = {"/tmp/omnicheck_s13_cov/bad.xml"};
    CHECK(oc_coverage_load(&cov2, bad, 1, &a, &st) != OC_OK);
    oc_coverage_destroy(&cov2);

    /* the JaCoCo reader still owns <report> files: the SPRINT-12 unit
     * corpus re-checked through the root-element dispatch */
    spew("/tmp/omnicheck_s13_cov/jacoco.xml",
         "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<report name=\"proj\">\n"
         "  <package name=\"app/store\">\n"
         "    <sourcefile name=\"Store.java\">\n"
         "      <method name=\"restock\" line=\"11\">\n"
         "        <counter type=\"LINE\" missed=\"0\" covered=\"1\"/>\n"
         "      </method>\n"
         "    </sourcefile>\n"
         "  </package>\n"
         "</report>\n");
    oc_coverage cov3;
    oc_coverage_init(&cov3);
    const char *jac[] = {"/tmp/omnicheck_s13_cov/jacoco.xml"};
    CHECK(oc_coverage_load(&cov3, jac, 1, &a, &st) == OC_OK);
    CHECK(oc_coverage_lookup(&cov3, "app/store/Store.java", 1, 100, &anchors, &covered));
    CHECK(anchors == 1 && covered == 1);
    oc_coverage_destroy(&cov3);

    oc_status_clear(&st);
    oc_arena_destroy(&a);
}

/* --- E2E: the three C# fixture corpora through the oc binary ------------- */
static void fixture_checks(void) {
    oc_arena a;
    oc_arena_init(&a);
    char bin[512], packs[512], snap[512], out1[512], out2[512], cmd[4096];
    snprintf(bin, sizeof(bin), "%s", OC_OC_BIN);
    snprintf(packs, sizeof(packs), "%s/packs", OC_REPO_ROOT);
    snprintf(snap, sizeof(snap), "%s/tests/fixtures/supply/osv_snapshot_csharp.json", OC_REPO_ROOT);
    snprintf(out1, sizeof(out1), "/tmp/omnicheck_s13_e2e1.json");
    snprintf(out2, sizeof(out2), "/tmp/omnicheck_s13_e2e2.json");

    /* 1) Corpus A (structural): the Go-spike counts, C# edition. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/tests/fixtures/csharp --packs-dir %s --format json"
             " > %s 2>/dev/null",
             bin, OC_REPO_ROOT, packs, out1);
    CHECK(run_cmd(cmd) == 1); /* findings exist: exit 1 at the warn threshold */
    {
        size_t rlen = 0;
        char *rd = NULL;
        FILE *fh = fopen(out1, "rb");
        fseek(fh, 0, SEEK_END);
        rlen = (size_t)ftell(fh);
        fseek(fh, 0, SEEK_SET);
        rd = malloc(rlen + 1);
        CHECK(fread(rd, 1, rlen, fh) == rlen);
        rd[rlen] = '\0';
        fclose(fh);
        oc_json *report = oc_json_parse(&a, rd, rlen, NULL);
        CHECK(report != NULL);
        if (report) {
            const oc_json *meta = oc_json_get(report, "meta");
            CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 5);
            CHECK(oc_json_as_num(oc_json_get(meta, "files_with_parse_errors"), -1) == 0);
            const oc_json *findings = oc_json_get(report, "findings");
            int counts[16] = {0};
            int oc006_program = 0;
            for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
                const oc_json *f = findings->items[i];
                const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
                int idx = atoi(rule + 2);
                if (idx > 0 && idx < 16)
                    counts[idx]++;
                if (strcmp(rule, "OC006") == 0 &&
                    strstr(oc_json_as_str(oc_json_get(f, "file"), ""), "Program.cs"))
                    oc006_program = 1;
            }
            CHECK(counts[1] == 1);  /* OC001: the 64-line method */
            CHECK(counts[2] == 1);  /* OC002: the TODO comment */
            CHECK(counts[4] == 1);  /* OC004: complexity 26 */
            CHECK(counts[6] == 4);  /* OC006: every non-entry file */
            CHECK(counts[8] == 2);  /* OC008: one group, 2 locations */
            CHECK(counts[11] == 1); /* OC011: the AWS example literal */
            CHECK(!oc006_program);  /* the static-Main parse fact claims Program.cs */
        }
        free(rd);
    }

    /* determinism */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/tests/fixtures/csharp --packs-dir %s --format json"
             " > %s 2>/dev/null",
             bin, OC_REPO_ROOT, packs, out2);
    CHECK(run_cmd(cmd) == 1);
    CHECK(files_equal(out1, out2));

    /* 2) Corpus B (resolver): the contracted edge set, decoy csproj
     *    never read BY THE RESOLVER, OC003 exactly once, OC005 zero. */
    {
        char gpath[512];
        snprintf(gpath, sizeof(gpath), "/tmp/omnicheck_s13_graph.json");
        snprintf(cmd, sizeof(cmd),
                 "%s graph %s/tests/fixtures/csharp_resolver --packs-dir %s > %s 2>/dev/null", bin,
                 OC_REPO_ROOT, packs, gpath);
        CHECK(run_cmd(cmd) == 0);
        size_t rlen = 0;
        FILE *fh = fopen(gpath, "rb");
        fseek(fh, 0, SEEK_END);
        rlen = (size_t)ftell(fh);
        fseek(fh, 0, SEEK_SET);
        char *rd = malloc(rlen + 1);
        CHECK(fread(rd, 1, rlen, fh) == rlen);
        rd[rlen] = '\0';
        fclose(fh);
        oc_json *graph = oc_json_parse(&a, rd, rlen, NULL);
        CHECK(graph != NULL);
        if (graph) {
            const oc_json *nodes = oc_json_get(graph, "nodes");
            const oc_json *edges = oc_json_get(graph, "edges");
            char paths[64][256];
            size_t nn = nodes ? oc_json_size(nodes) : 0;
            for (size_t i = 0; i < nn; i++) {
                const oc_json *n = nodes->items[i];
                snprintf(paths[i], sizeof(paths[i]), "%s",
                         oc_json_as_str(oc_json_get(n, "path"), ""));
            }
            long n_edges = 0;
            int e_p_store = 0, e_p_types = 0, e_p_info = 0, e_p_inv = 0, e_p_deep = 0, e_ab = 0,
                e_ba = 0, e_c_store = 0, e_c_types = 0, e_c_info = 0, e_c_inv = 0, e_external = 0;
            for (size_t i = 0; edges && i < oc_json_size(edges); i++) {
                const oc_json *e = edges->items[i];
                if (strcmp(oc_json_as_str(oc_json_get(e, "kind"), ""), "imports") != 0)
                    continue;
                n_edges++;
                double from = oc_json_as_num(oc_json_get(e, "from"), -1);
                double to = oc_json_as_num(oc_json_get(e, "to"), -1);
                const char *fp = (from >= 0 && (size_t)from < nn) ? paths[(size_t)from] : "";
                const char *tp = (to >= 0 && (size_t)to < nn) ? paths[(size_t)to] : "";
                if (strstr(fp, "App/Program.cs") && strstr(tp, "Store/Store.cs"))
                    e_p_store = 1;
                if (strstr(fp, "App/Program.cs") && strstr(tp, "Store/Types.cs"))
                    e_p_types = 1;
                if (strstr(fp, "App/Program.cs") && strstr(tp, "Store/StoreInfo.cs"))
                    e_p_info = 1;
                if (strstr(fp, "App/Program.cs") && strstr(tp, "Store/Inventory.cs"))
                    e_p_inv = 1;
                if (strstr(fp, "App/Program.cs") && strstr(tp, "Store/Deep/Nested.cs"))
                    e_p_deep = 1;
                if (strstr(fp, "A/A.cs") && strstr(tp, "B/B.cs"))
                    e_ab = 1;
                if (strstr(fp, "B/B.cs") && strstr(tp, "A/A.cs"))
                    e_ba = 1;
                if (strstr(fp, "Client/Client.cs") && strstr(tp, "Store/Store.cs"))
                    e_c_store = 1;
                if (strstr(fp, "Client/Client.cs") && strstr(tp, "Store/Types.cs"))
                    e_c_types = 1;
                if (strstr(fp, "Client/Client.cs") && strstr(tp, "Store/StoreInfo.cs"))
                    e_c_info = 1;
                if (strstr(fp, "Client/Client.cs") && strstr(tp, "Store/Inventory.cs"))
                    e_c_inv = 1;
                if (strstr(tp, "external") || strstr(tp, "res.txt"))
                    e_external = 1; /* the csproj coordinate / a resource: never resolved */
            }
            CHECK(n_edges == 11);
            CHECK(e_p_store); /* namespace + static + alias all -> ONE edge */
            CHECK(e_p_types); /* the namespace fan-out, direct children */
            CHECK(e_p_info);
            CHECK(e_p_inv);      /* the alias-type reading (deduped with the fan-out) */
            CHECK(e_p_deep);     /* the alias-namespace reading claims the subdirectory */
            CHECK(e_ab && e_ba); /* the A/B namespace cycle */
            CHECK(e_c_store && e_c_types && e_c_info && e_c_inv); /* Client fan-out (4) */
            CHECK(!e_external);
        }
        free(rd);
    }

    /* corpus B findings: OC003 exactly once (2-file cycle), OC005 zero,
     * OC006 exactly the two unreferenced files, decoy csproj parsed by
     * the SUPPLY tier only. */
    snprintf(cmd, sizeof(cmd),
             "%s check %s/tests/fixtures/csharp_resolver --packs-dir %s"
             " --format json > %s 2>/dev/null",
             bin, OC_REPO_ROOT, packs, out1);
    CHECK(run_cmd(cmd) == 1);
    {
        size_t rlen = 0;
        FILE *fh = fopen(out1, "rb");
        fseek(fh, 0, SEEK_END);
        rlen = (size_t)ftell(fh);
        fseek(fh, 0, SEEK_SET);
        char *rd = malloc(rlen + 1);
        CHECK(fread(rd, 1, rlen, fh) == rlen);
        rd[rlen] = '\0';
        fclose(fh);
        oc_json *report = oc_json_parse(&a, rd, rlen, NULL);
        CHECK(report != NULL);
        if (report) {
            const oc_json *meta = oc_json_get(report, "meta");
            CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 10);
            CHECK(oc_json_as_num(oc_json_get(meta, "files_with_parse_errors"), -1) == 0);
            const oc_json *supply = oc_json_get(meta, "supply");
            CHECK(oc_json_as_num(oc_json_get(supply, "manifests_scanned"), -1) == 1);
            const oc_json *findings = oc_json_get(report, "findings");
            int oc003 = 0, oc005 = 0, oc006 = 0, oc010 = 0, other = 0;
            for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
                const oc_json *f = findings->items[i];
                const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
                if (strcmp(rule, "OC003") == 0)
                    oc003++;
                else if (strcmp(rule, "OC005") == 0)
                    oc005++;
                else if (strcmp(rule, "OC006") == 0)
                    oc006++;
                else if (strcmp(rule, "OC010") == 0)
                    oc010++; /* decoy csproj HAS a licence expression: must stay 0 */
                else
                    other++;
            }
            CHECK(oc003 == 1);
            CHECK(oc005 == 0);
            CHECK(oc006 == 2); /* Client, Lonely */
            CHECK(oc010 == 0); /* the licence expression is detected */
            CHECK(other == 0);
        }
        free(rd);
    }

    /* 3) csharp_manifest: csproj tier + Cobertura composition. The
     *    Cobertura report is generated at test time so the class
     *    filename join carries the analyzed paths as a "/"-boundary
     *    tail (the format's own filenames are project-relative). */
    {
        char cob[512];
        snprintf(cob, sizeof(cob), "/tmp/omnicheck_s13_cobertura.xml");
        FILE *jf = fopen(cob, "wb");
        CHECK(jf != NULL);
        if (jf) {
            fprintf(jf, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
            fprintf(jf, "<coverage line-rate=\"0.5\" version=\"6.0.4\">\n");
            fprintf(jf, "  <packages><package name=\"gen\"><classes>\n");
            fprintf(jf,
                    "    <class name=\"Alpha.Lib\" filename=\"%s/tests/fixtures"
                    "/csharp_manifest/alpha/Alpha/Lib.cs\">\n",
                    OC_REPO_ROOT);
            for (int ln = 1; ln <= 6; ln++)
                fprintf(jf, "      <line number=\"%d\" hits=\"3\" branch=\"False\" />\n", ln);
            fprintf(jf, "    </class>\n");
            fprintf(jf,
                    "    <class name=\"Beta.Grade\" filename=\"%s/tests/fixtures"
                    "/csharp_manifest/beta/Grade.cs\">\n",
                    OC_REPO_ROOT);
            for (int ln = 1; ln <= 13; ln++)
                fprintf(jf, "      <line number=\"%d\" hits=\"0\" branch=\"False\" />\n", ln);
            fprintf(jf, "    </class>\n");
            fprintf(jf, "  </classes></package></packages>\n");
            fprintf(jf, "</coverage>\n");
            fclose(jf);
        }
        snprintf(cmd, sizeof(cmd),
                 "%s check %s/tests/fixtures/csharp_manifest --packs-dir %s"
                 " --coverage %s --format json > %s 2>/dev/null",
                 bin, OC_REPO_ROOT, packs, cob, out1);
        CHECK(run_cmd(cmd) == 1);
        size_t rlen = 0;
        FILE *fh = fopen(out1, "rb");
        fseek(fh, 0, SEEK_END);
        rlen = (size_t)ftell(fh);
        fseek(fh, 0, SEEK_SET);
        char *rd = malloc(rlen + 1);
        CHECK(fread(rd, 1, rlen, fh) == rlen);
        rd[rlen] = '\0';
        fclose(fh);
        oc_json *report = oc_json_parse(&a, rd, rlen, NULL);
        CHECK(report != NULL);
        if (report) {
            const oc_json *meta = oc_json_get(report, "meta");
            const oc_json *covmeta = oc_json_get(meta, "coverage");
            CHECK(oc_json_as_num(oc_json_get(covmeta, "files_loaded"), -1) == 2);
            CHECK(oc_json_as_num(oc_json_get(covmeta, "files_matched"), -1) == 2);
            CHECK(oc_json_as_num(oc_json_get(covmeta, "functions_measured"), -1) == 2);
            const oc_json *findings = oc_json_get(report, "findings");
            int oc006 = 0, oc007 = 0, oc010 = 0, other = 0;
            for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
                const oc_json *f = findings->items[i];
                const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
                if (strcmp(rule, "OC006") == 0)
                    oc006++;
                else if (strcmp(rule, "OC007") == 0) {
                    oc007++;
                    CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f, "message"), ""), "CRAP=42");
                } else if (strcmp(rule, "OC010") == 0) {
                    oc010++;
                    CHECK(strstr(oc_json_as_str(oc_json_get(f, "file"), ""), "beta/beta.csproj") !=
                          NULL);
                } else {
                    other++;
                }
            }
            CHECK(oc007 == 1); /* Grade: comp 6, 0/13 anchors -> CRAP 42 */
            CHECK(oc010 == 1); /* beta has no licence at all; alpha does */
            CHECK(oc006 == 2); /* Grade; Program.cs is claimed by the
                                * top-level-statements fact; Lib.cs is
                                * used only within its own namespace (no
                                * using directive needed in C# -- the
                                * v1 modeling boundary, honestly
                                * invisible to import-driven marking) */
            CHECK(other == 0); /* OC009 silent without a snapshot */
        }
        free(rd);

        /* with the NuGet OSV snapshot: OC009 fires 5 times (2 pinned
         * matches + 3 unpinned warns), all anchored at line 1 (the XML
         * reader does not track lines -- never guessed) */
        snprintf(cmd, sizeof(cmd),
                 "%s check %s/tests/fixtures/csharp_manifest --packs-dir %s"
                 " --osv-snapshot %s --format json > %s 2>/dev/null",
                 bin, OC_REPO_ROOT, packs, snap, out2);
        CHECK(run_cmd(cmd) == 1);
        fh = fopen(out2, "rb");
        fseek(fh, 0, SEEK_END);
        rlen = (size_t)ftell(fh);
        fseek(fh, 0, SEEK_SET);
        rd = malloc(rlen + 1);
        CHECK(fread(rd, 1, rlen, fh) == rlen);
        rd[rlen] = '\0';
        fclose(fh);
        report = oc_json_parse(&a, rd, rlen, NULL);
        CHECK(report != NULL);
        if (report) {
            const oc_json *findings = oc_json_get(report, "findings");
            int oc009 = 0, oc009_match = 0, oc009_unpinned = 0, oc010 = 0;
            for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
                const oc_json *f = findings->items[i];
                const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
                const char *msg = oc_json_as_str(oc_json_get(f, "message"), "");
                double line = oc_json_as_num(oc_json_get(f, "line"), -1);
                if (strcmp(rule, "OC009") == 0) {
                    oc009++;
                    CHECK(line == 1);
                    if (strstr(msg, "matched the vendored OSV snapshot"))
                        oc009_match++;
                    if (strstr(msg, "is not pinned; skipped"))
                        oc009_unpinned++;
                } else if (strcmp(rule, "OC010") == 0) {
                    oc010++;
                }
            }
            CHECK(oc009 == 5);
            CHECK(oc009_match == 2);    /* Vuln.Lib 1.2.0 + Indirect.Lib 0.4.0 */
            CHECK(oc009_unpinned == 3); /* range + ${property} + floating */
            CHECK(oc010 == 1);
        }
        free(rd);

        /* a malformed Cobertura record fails loud: nonzero exit */
        spew("/tmp/omnicheck_s13_bad.xml",
             "<?xml version=\"1.0\"?><coverage><packages><package name=\"p\">"
             "<classes><class name=\"X\" filename=\"X.cs\"><lines><line hits=\"1\"/>"
             "</lines></class></classes></package></coverage>");
        snprintf(cmd, sizeof(cmd),
                 "%s check %s/tests/fixtures/csharp_manifest --packs-dir %s"
                 " --coverage /tmp/omnicheck_s13_bad.xml > /dev/null 2>&1",
                 bin, OC_REPO_ROOT, packs);
        CHECK(run_cmd(cmd) != 0);
    }

    oc_arena_destroy(&a);
}

int main(void) {
    unit_csharp_resolver();
    unit_csharp_marking();
    unit_csproj();
    unit_cobertura();
    fixture_checks();
    TEST_SUMMARY();
}
