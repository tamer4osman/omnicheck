/* Sprint 11 (Track D wave 1, Java adoption): the whole pre-approved
 * contract of docs/LANGUAGES.md §Java lands in one sprint -- grammar
 * (vendored tree-sitter-java v0.23.5), resolver v1-java (class-file
 * model, wildcard package claims, nested-class never-guess), the
 * pre-committed JaCoCo XML coverage adapter and pom.xml supply support
 * (OSV ecosystem Maven). This test pins the resolver semantics and the
 * adapter facts at unit level and the two fixture corpora end to end
 * through the oc binary. */
#include "oc/common.h"
#include "oc/coverage.h"
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

/* --- unit: resolver v1-java semantics ---------------------------------- */
static void unit_java_resolver(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);

    /* Layout: the store package has two classes and a test file, plus a
     * subpackage (direct-children rule) and a DECOY nested-class file
     * shape (deep.Nested exists, deep.Nested.Inner does not -- the
     * nested-class import must never fabricate it). */
    oc_graph_add_file(&g, "proj/com/example/store/Store.java");
    oc_graph_add_file(&g, "proj/com/example/store/Types.java");
    oc_graph_add_file(&g, "proj/com/example/store/StoreTest.java");
    oc_graph_add_file(&g, "proj/com/example/store/sub/Inner.java");
    oc_graph_add_file(&g, "proj/com/example/deep/Nested.java");
    oc_graph_add_file(&g, "proj/app/Main.java");

    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }

    /* Single-type import: the class-file model, dotted-to-slash. */
    const char *t =
        oc_resolve_specifier(cx, &a, "proj/app/Main.java", "com.example.store.Store", OC_LANG_JAVA);
    CHECK_STR_EQ(t, "proj/com/example/store/Store.java");
    /* Static qualifier: the member was stripped at parse time; the
     * qualifier resolves like a single-type import. */
    t = oc_resolve_specifier(cx, &a, "proj/app/Main.java", "com.example.store.Types", OC_LANG_JAVA);
    CHECK_STR_EQ(t, "proj/com/example/store/Types.java");
    /* The importing file's location never narrows a Java qualifier (the
     * same result from a different directory). */
    t = oc_resolve_specifier(cx, &a, "proj/com/example/store/Store.java", "com.example.deep.Nested",
                             OC_LANG_JAVA);
    CHECK_STR_EQ(t, "proj/com/example/deep/Nested.java");
    /* NESTED CLASSES never guess: a/b/C/D.java does not exist and v1
     * never falls back to a/b/C.java (docs/LANGUAGES.md §Java.5). */
    t = oc_resolve_specifier(cx, &a, "proj/app/Main.java", "com.example.deep.Nested.Inner",
                             OC_LANG_JAVA);
    CHECK(t == NULL);
    /* Subpackage: its own directory, reachable through the same mapping. */
    t = oc_resolve_specifier(cx, &a, "proj/app/Main.java", "com.example.store.sub.Inner",
                             OC_LANG_JAVA);
    CHECK_STR_EQ(t, "proj/com/example/store/sub/Inner.java");
    /* Wildcard package claims never collapse into one file. */
    t = oc_resolve_specifier(cx, &a, "proj/app/Main.java", "com.example.store.*", OC_LANG_JAVA);
    CHECK(t == NULL);
    /* JDK and everything outside the analyzed set: unresolved. */
    t = oc_resolve_specifier(cx, &a, "proj/app/Main.java", "java.util.ArrayList", OC_LANG_JAVA);
    CHECK(t == NULL);

    /* Package directory: exists iff a .java file sits DIRECTLY inside. */
    const char *dir = oc_resolve_java_pkg_dir(cx, "com.example.store.*", &a);
    CHECK_STR_EQ(dir, "proj/com/example/store");
    dir = oc_resolve_java_pkg_dir(cx, "com.example.store", &a);
    CHECK_STR_EQ(dir, "proj/com/example/store");
    /* The subdirectory is NOT part of the parent's package claim. */
    dir = oc_resolve_java_pkg_dir(cx, "com.example.store.sub", &a);
    CHECK_STR_EQ(dir, "proj/com/example/store/sub");
    /* A file is not a package: no directory named Store.java. */
    CHECK(oc_resolve_java_pkg_dir(cx, "com.example.store.Store", &a) == NULL);

    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: never-guess guards ------------------------------------------ */
static void unit_java_guards(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_graph_add_file(&g, "proj/com/example/store/Store.java");
    const char *roots[1] = {"proj"};
    oc_resolve_ctx *cx = oc_resolve_ctx_new(&a, &g, roots, 1);
    CHECK(cx != NULL);
    if (!cx) {
        oc_graph_destroy(&g);
        oc_arena_destroy(&a);
        return;
    }
    /* Path escapes and malformed qualifiers stay unresolved, always. */
    CHECK(oc_resolve_specifier(cx, &a, "proj/app/Main.java", ".example.Store", OC_LANG_JAVA) ==
          NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/app/Main.java", "com..example.Store", OC_LANG_JAVA) ==
          NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/app/Main.java", "com.example..Store", OC_LANG_JAVA) ==
          NULL);
    CHECK(oc_resolve_specifier(cx, &a, "proj/app/Main.java", "/abs/java/Path", OC_LANG_JAVA) ==
          NULL);
    CHECK(oc_resolve_java_pkg_dir(cx, "..", &a) == NULL);
    CHECK(oc_resolve_java_pkg_dir(cx, "com/evil", &a) == NULL);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: wildcard package marking (fail-open surface) ---------------- */
static void unit_java_marking(void) {
    oc_arena a;
    oc_arena_init(&a);
    oc_status st;
    oc_graph g;
    oc_graph_init(&g, &a);
    oc_graph_add_file(&g, "proj/com/example/store/Store.java");
    oc_graph_add_file(&g, "proj/com/example/store/sub/Inner.java");
    oc_graph_add_file(&g, "proj/app/Main.java");

    oc_bind_vec binds;
    oc_surface_binds_init(&binds);
    /* The wildcard import claims the package: binding name NULL, the
     * fail-open marking happens in the surface build. */
    CHECK(oc_surface_bind_push(&binds, &a, "proj/app/Main.java", "com.example.store.*", NULL,
                               OC_LANG_JAVA));

    oc_export_vec exports;
    oc_surface_exports_init(&exports);
    CHECK(oc_surface_export_push(&exports, &a, "proj/com/example/store/Store.java", "Store", 3, 0));
    CHECK(oc_surface_export_push(&exports, &a, "proj/com/example/store/sub/Inner.java", "Inner", 3,
                                 0));

    oc_hint_vec hints;
    oc_surface_hints_init(&hints);
    const char *roots[1] = {"proj"};
    oc_surface *s = oc_surface_build(&a, &g, &binds, &exports, &hints, roots, 1, NULL, &st);
    CHECK(s != NULL);
    if (s) {
        CHECK(s->binds_resolved == 1);
        CHECK(exports.n == 2);
        CHECK(exports.items[0].used == 1); /* package claim = fail-open */
        CHECK(exports.items[1].used == 0); /* subdirectory: separate package */
    }

    oc_surface_hints_destroy(&hints);
    oc_surface_binds_destroy(&binds);
    oc_surface_exports_destroy(&exports);
    oc_graph_destroy(&g);
    oc_arena_destroy(&a);
}

/* --- unit: pom.xml loader (OC009/OC010 facts) -------------------------- */
static void unit_pom_loader(void) {
    oc_arena a;
    oc_arena_init(&a);
    const char *path = "/tmp/pom.xml";
    FILE *fh = fopen(path, "w");
    CHECK(fh != NULL);
    if (!fh)
        goto done;
    fputs("<?xml version=\"1.0\"?>\n"
          "<project xmlns=\"http://maven.apache.org/POM/4.0.0\">\n"
          "  <licenses><license><name>Apache-2.0</name></license></licenses>\n"
          "  <dependencyManagement><dependencies>\n"
          "    <dependency><groupId>org.junit</groupId><artifactId>junit</artifactId>"
          "<version>5.0.0</version></dependency>\n"
          "  </dependencies></dependencyManagement>\n"
          "  <dependencies>\n"
          "    <dependency><groupId>com.example.ghost</groupId><artifactId>ghost</artifactId>"
          "<version>1.2.3</version></dependency>\n"
          "    "
          "<dependency><groupId>org.apache</groupId><artifactId>commons</artifactId></dependency>\n"
          "    <dependency><groupId>org.props</groupId><artifactId>prop-art</artifactId>"
          "<version>${prop.version}</version></dependency>\n"
          "    <dependency><groupId>com.wide</groupId><artifactId>range-art</artifactId>"
          "<version>[1.0,2.0)</version></dependency>\n"
          "  </dependencies>\n"
          "</project>\n",
          fh);
    fclose(fh);

    const char *files[1] = {path};
    oc_manifest_vec mv;
    oc_status st;
    CHECK(oc_supply_load_manifests(&mv, files, 1, &a, &st) == OC_OK);
    CHECK(mv.n == 1);
    if (mv.n == 1) {
        const oc_manifest *m = &mv.items[0];
        CHECK(m->kind == OC_MANIFEST_POM);
        CHECK(m->eco == OC_ECO_MAVEN);
        CHECK_STR_EQ(m->license, "Apache-2.0");
        /* Only the root-level <dependencies>: 4 deps (the
         * dependencyManagement block is not a dependency). */
        CHECK(m->n_deps == 4);
        int saw_ghost_pin = 0, saw_commons_unpinned = 0, saw_prop_unpinned = 0,
            saw_range_unpinned = 0, saw_junit = 0;
        for (size_t i = 0; i < m->n_deps; i++) {
            const oc_dep *d = &m->deps[i];
            if (strcmp(d->name, "com.example.ghost:ghost") == 0 && d->pinned &&
                strcmp(d->version, "1.2.3") == 0)
                saw_ghost_pin = 1;
            if (strcmp(d->name, "org.apache:commons") == 0 && !d->pinned && d->version == NULL)
                saw_commons_unpinned = 1;
            if (strcmp(d->name, "org.props:prop-art") == 0 && !d->pinned)
                saw_prop_unpinned = 1; /* ${...} never substituted */
            if (strcmp(d->name, "com.wide:range-art") == 0 && !d->pinned)
                saw_range_unpinned = 1; /* Maven range never guessed */
            if (strcmp(d->name, "org.junit:junit") == 0)
                saw_junit = 1; /* managed dep must NOT appear */
        }
        CHECK(saw_ghost_pin);
        CHECK(saw_commons_unpinned);
        CHECK(saw_prop_unpinned);
        CHECK(saw_range_unpinned);
        CHECK(!saw_junit);
    }
    oc_manifests_destroy(&mv);
done:
    oc_arena_destroy(&a);
}

/* --- unit: JaCoCo XML adapter (OC007 facts) ----------------------------- */
static void unit_jacoco_loader(void) {
    oc_arena a;
    oc_arena_init(&a);
    const char *path = "/tmp/oc_s11_jacoco.xml";
    FILE *fh = fopen(path, "w");
    CHECK(fh != NULL);
    if (!fh)
        goto done;
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE report SYSTEM \"jacoco.dtd\">\n"
          "<report name=\"fixture\">\n"
          "  <package name=\"com/example/util\">\n"
          "    <sourcefile name=\"TextUtil.java\">\n"
          "      <method name=\"describe\" line=\"15\">\n"
          "        <counter type=\"INSTRUCTION\" missed=\"9\" covered=\"1\"/>\n"
          "        <counter type=\"LINE\" missed=\"0\" covered=\"4\"/>\n"
          "      </method>\n"
          "      <method name=\"names\" line=\"75\">\n"
          "        <counter type=\"LINE\" missed=\"3\" covered=\"0\"/>\n"
          "      </method>\n"
          "    </sourcefile>\n"
          "  </package>\n"
          "</report>\n",
          fh);
    fclose(fh);

    oc_coverage cov;
    oc_coverage_init(&cov);
    oc_status st;
    const char *files[1] = {path};
    CHECK(oc_coverage_load(&cov, files, 1, &a, &st) == OC_OK);
    CHECK(cov.n_files == 1);
    CHECK(cov.files_loaded == 1);
    if (cov.n_files == 1) {
        CHECK_STR_EQ(cov.files[0].path, "com/example/util/TextUtil.java");
        CHECK(cov.files[0].n_anchor == 2);  /* one anchor per method line */
        CHECK(cov.files[0].n_covered == 1); /* only describe has covered > 0 */
        /* The standard path rule, package-relative direction: the report
         * path is the analyzed path's "/"-boundary tail. */
        size_t anchors = 0, covered = 0;
        CHECK(oc_coverage_lookup(&cov, "src/main/java/com/example/util/TextUtil.java", 10, 40,
                                 &anchors, &covered) == 1);
        CHECK(anchors == 1); /* describe's line 15 inside the span; names' 75 outside */
        CHECK(covered == 1);
    }
    oc_coverage_destroy(&cov);
done:
    oc_arena_destroy(&a);
}

/* --- fixtures through the oc binary ------------------------------------- */
static int files_equal(const char *p1, const char *p2) {
    char *d1 = NULL, *d2 = NULL;
    size_t l1 = 0, l2 = 0;
    if (oc_read_file(p1, &d1, &l1, NULL) != OC_OK)
        return 0;
    if (oc_read_file(p2, &d2, &l2, NULL) != OC_OK) {
        free(d1);
        return 0;
    }
    int eq = l1 == l2 && (l1 == 0 || memcmp(d1, d2, l1) == 0);
    free(d1);
    free(d2);
    return eq;
}

static void fixture_checks(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char fix[512], fixa[512], jac[512], packs[512], out1[256], out2[256], cmd[2048];
    snprintf(fix, sizeof(fix), "%s/tests/fixtures/java_resolver", root);
    snprintf(fixa, sizeof(fixa), "%s/tests/fixtures/java", root);
    snprintf(jac, sizeof(jac), "%s/tests/fixtures/java_cov/jacoco.xml", root);
    snprintf(packs, sizeof(packs), "%s/packs", root);
    snprintf(out1, sizeof(out1), "/tmp/oc_s11_out1.json");
    snprintf(out2, sizeof(out2), "/tmp/oc_s11_out2.json");

    oc_arena arena;
    oc_arena_init(&arena);

    /* 1) Resolver corpus graph: exactly ELEVEN imports edges -- the
     * store wildcard fan-out (Store/Types/StoreTest -- a Java wildcard
     * claims the whole analyzed package, test files included), the
     * duplicate single-type + static records (Store again, Types
     * again), the util wildcard (Helper), Helper -> Nested, and the
     * Store/Types cycle. The nested-class import, the JDK and the
     * pom-named ghost stay unresolved: the pom is never read by the
     * resolver. */
    snprintf(cmd, sizeof(cmd), "%s graph %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out1);
    CHECK(run_cmd(cmd) == 0);
    char *data = NULL;
    size_t len = 0;
    oc_json *graph = NULL;
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        graph = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(graph != NULL);
    if (graph) {
        const oc_json *nodes = oc_json_get(graph, "nodes");
        char *path_by_id[256] = {0};
        for (size_t i = 0; nodes && i < oc_json_size(nodes); i++) {
            const oc_json *n = nodes->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(n, "kind"), ""), "file") != 0)
                continue;
            size_t id = (size_t)oc_json_as_num(oc_json_get(n, "id"), -1);
            if (id < 256)
                path_by_id[id] = (char *)oc_json_as_str(oc_json_get(n, "path"), "");
        }
        const oc_json *edges = oc_json_get(graph, "edges");
        size_t imports_edges = 0;
        int main_store = 0, main_types = 0, main_test = 0, main_helper = 0, helper_nested = 0,
            store_types = 0, types_store = 0, stray = 0;
        for (size_t i = 0; edges && i < oc_json_size(edges); i++) {
            const oc_json *e = edges->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(e, "kind"), ""), "imports") != 0)
                continue;
            imports_edges++;
            size_t f = (size_t)oc_json_as_num(oc_json_get(e, "from"), -1);
            size_t t = (size_t)oc_json_as_num(oc_json_get(e, "to"), -1);
            const char *fp = f < 256 ? path_by_id[f] : "";
            const char *tp = t < 256 ? path_by_id[t] : "";
            if (strstr(fp, "app/Main.java") && strstr(tp, "store/Store.java"))
                main_store++;
            if (strstr(fp, "app/Main.java") && strstr(tp, "store/Types.java"))
                main_types++;
            if (strstr(fp, "app/Main.java") && strstr(tp, "store/StoreTest.java"))
                main_test++;
            if (strstr(fp, "app/Main.java") && strstr(tp, "util/Helper.java"))
                main_helper++;
            if (strstr(fp, "util/Helper.java") && strstr(tp, "deep/Nested.java"))
                helper_nested++;
            if (strstr(fp, "store/Store.java") && strstr(tp, "store/Types.java"))
                store_types++;
            if (strstr(fp, "store/Types.java") && strstr(tp, "store/Store.java"))
                types_store++;
            if (!strstr(fp, "app/Main.java") && !strstr(fp, "store/Store.java") &&
                !strstr(fp, "store/Types.java") && !strstr(fp, "util/Helper.java"))
                stray = 1;
        }
        CHECK(imports_edges == 11);
        CHECK(main_store == 3); /* single-type + static qualifier + wildcard */
        CHECK(main_types == 2); /* single-type + wildcard */
        CHECK(main_test == 2);  /* single-type + wildcard: test files join a package claim */
        CHECK(main_helper == 1);
        CHECK(helper_nested == 1);
        CHECK(store_types == 1);
        CHECK(types_store == 1);
        CHECK(!stray); /* no edge through the pom, the JDK or the nested class */
    }

    /* 2) Resolver corpus findings: OC003 fires once (the 2-file cycle);
     * OC005 is silent by construction (single-type imports bind the
     * class name Java requires, wildcards fail-open -- the same
     * documented empty-verdict consequence as §Go); OC006 fires only on
     * the lonely file (StoreTest is a Surefire convention entry,
     * Main.java a pack glob entry); OC010 fires on the licence-less
     * pom (the supply tier, not the resolver); OC009 is silent without
     * an OSV snapshot. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out1);
    CHECK(run_cmd(cmd) == 1);
    oc_json *report = NULL;
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        report = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(report != NULL);
    if (report) {
        const oc_json *findings = oc_json_get(report, "findings");
        int oc003 = 0, oc005 = 0, oc006 = 0, oc006_lonely = 0, oc006_test = 0, oc010 = 0, oc009 = 0,
            other = 0;
        for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
            const oc_json *f = findings->items[i];
            const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
            const char *file = oc_json_as_str(oc_json_get(f, "file"), "");
            if (strcmp(rule, "OC003") == 0) {
                oc003++;
                CHECK_STR_CONTAINS(oc_json_as_str(oc_json_get(f, "message"), ""),
                                   "Import cycle among 2 files");
            } else if (strcmp(rule, "OC005") == 0) {
                oc005++;
            } else if (strcmp(rule, "OC006") == 0) {
                oc006++;
                if (strstr(file, "lonely/Lonely.java"))
                    oc006_lonely = 1;
                if (strstr(file, "StoreTest.java"))
                    oc006_test = 1;
            } else if (strcmp(rule, "OC010") == 0) {
                oc010++;
                CHECK_STR_CONTAINS(file, "pom.xml");
            } else if (strcmp(rule, "OC009") == 0) {
                oc009++;
            } else {
                other++;
            }
        }
        CHECK(oc003 == 1);
        CHECK(oc005 == 0); /* the documented empty verdict set (§Java.6) */
        CHECK(oc006 == 1);
        CHECK(oc006_lonely);
        CHECK(!oc006_test); /* Surefire convention claiming proved by absence */
        CHECK(oc010 == 1);
        CHECK(oc009 == 0); /* no OSV snapshot: never guessed, meta says so */
        CHECK(other == 0);
        const oc_json *meta = oc_json_get(report, "meta");
        CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 7);
        CHECK(oc_json_as_num(oc_json_get(meta, "files_with_parse_errors"), -1) == 0);
    }

    /* 3) Structural corpus A: exactly the contracted counts; module-info
     * and package-info parse cleanly with ZERO function symbols; the
     * describe method carries complexity 25; no imports edges. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fixa, packs,
             out1);
    CHECK(run_cmd(cmd) == 1); /* findings present */
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        report = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(report != NULL);
    if (report) {
        const oc_json *findings = oc_json_get(report, "findings");
        int oc001 = 0, oc002 = 0, oc003 = 0, oc004 = 0, oc005 = 0, oc006 = 0, oc008 = 0, oc010 = 0,
            oc011 = 0, other = 0;
        for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
            const oc_json *f = findings->items[i];
            const char *rule = oc_json_as_str(oc_json_get(f, "rule"), "");
            if (strcmp(rule, "OC001") == 0)
                oc001++;
            else if (strcmp(rule, "OC002") == 0)
                oc002++;
            else if (strcmp(rule, "OC003") == 0)
                oc003++;
            else if (strcmp(rule, "OC004") == 0)
                oc004++;
            else if (strcmp(rule, "OC005") == 0)
                oc005++;
            else if (strcmp(rule, "OC006") == 0)
                oc006++;
            else if (strcmp(rule, "OC008") == 0)
                oc008++;
            else if (strcmp(rule, "OC010") == 0)
                oc010++;
            else if (strcmp(rule, "OC011") == 0)
                oc011++;
            else
                other++;
        }
        CHECK(oc001 == 1);
        CHECK(oc002 == 1);
        CHECK(oc003 == 0); /* corpus A is edge-free by construction */
        CHECK(oc004 == 1);
        CHECK(oc005 == 0);
        CHECK(oc006 == 8); /* every non-entry file (Main.java is the glob entry) */
        CHECK(oc008 == 2); /* one duplicate group, 2 locations */
        CHECK(oc010 == 0); /* no pom in corpus A */
        CHECK(oc011 == 1);
        CHECK(other == 0);
        const oc_json *meta = oc_json_get(report, "meta");
        CHECK(oc_json_as_num(oc_json_get(meta, "files_analyzed"), -1) == 9);
        CHECK(oc_json_as_num(oc_json_get(meta, "files_with_parse_errors"), -1) == 0);
    }

    /* 4) Corpus A + JaCoCo coverage: OC007 fires exactly once, on the
     * uncovered describe method (CRAP over threshold); names is
     * covered and adds none. */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s --coverage %s > %s 2>/dev/null", bin,
             fixa, packs, jac, out1);
    CHECK(run_cmd(cmd) == 1); /* findings present */
    if (oc_read_file(out1, &data, &len, NULL) == OC_OK) {
        report = oc_json_parse(&arena, data, len, NULL);
        free(data);
    }
    CHECK(report != NULL);
    if (report) {
        const oc_json *findings = oc_json_get(report, "findings");
        int oc007 = 0, oc007_describe = 0;
        for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
            const oc_json *f = findings->items[i];
            if (strcmp(oc_json_as_str(oc_json_get(f, "rule"), ""), "OC007") != 0)
                continue;
            oc007++;
            if (strstr(oc_json_as_str(oc_json_get(f, "symbol"), ""), "describe"))
                oc007_describe = 1;
        }
        CHECK(oc007 == 1);
        CHECK(oc007_describe);
        const oc_json *meta = oc_json_get(report, "meta");
        const oc_json *cov = oc_json_get(meta, "coverage");
        CHECK(oc_json_as_num(oc_json_get(cov, "files_matched"), -1) == 1);
        CHECK(oc_json_as_num(oc_json_get(cov, "functions_measured"), -1) == 2);
    }

    /* 5) Determinism: byte-identical reruns of both corpora (fresh
     * output names -- out1 currently holds the coverage run). */
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fixa, packs,
             "/tmp/oc_s11_a_plain.json");
    CHECK(run_cmd(cmd) == 1);
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fixa, packs,
             out2);
    CHECK(run_cmd(cmd) == 1);
    CHECK(files_equal("/tmp/oc_s11_a_plain.json", out2));
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             "/tmp/oc_s11_b_plain.json");
    CHECK(run_cmd(cmd) == 1);
    snprintf(cmd, sizeof(cmd), "%s check %s --packs-dir %s > %s 2>/dev/null", bin, fix, packs,
             out2);
    CHECK(run_cmd(cmd) == 1);
    CHECK(files_equal("/tmp/oc_s11_b_plain.json", out2));

    oc_arena_destroy(&arena);
}

int main(void) {
    unit_java_resolver();
    unit_java_guards();
    unit_java_marking();
    unit_pom_loader();
    unit_jacoco_loader();
    fixture_checks();
    TEST_SUMMARY();
}
