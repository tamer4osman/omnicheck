#define _DARWIN_C_SOURCE /* make mkdtemp visible on macOS clang */
#include "oc/discover.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void touch(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fputs("x\n", f);
    fclose(f);
}

int main(void) {
    char tmpl[] = "/tmp/oc_disc_test_XXXXXX";
    char *root = mkdtemp(tmpl);
    CHECK(root != NULL);
    if (!root)
        return 1;

    char sub[256], ignored[256], hidden[256], deep[256];
    snprintf(sub, sizeof(sub), "%s/sub", root);
    snprintf(ignored, sizeof(ignored), "%s/node_modules", root);
    snprintf(hidden, sizeof(hidden), "%s/.git", root);
    snprintf(deep, sizeof(deep), "%s/sub/deep", root);
    mkdir(sub, 0755);
    mkdir(ignored, 0755);
    mkdir(hidden, 0755);
    mkdir(deep, 0755);

    char p[512];
    snprintf(p, sizeof(p), "%s/a.js", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/b.py", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/notes.md", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/sub/c.js", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/sub/deep/d.py", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/node_modules/e.js", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/.git/config", root);
    touch(p);
    snprintf(p, sizeof(p), "%s/.hidden.js", root);
    touch(p);

    /* In-memory pack set: .js and .py extensions. */
    oc_arena a;
    oc_arena_init(&a);
    oc_pack local_packs[2];
    memset(local_packs, 0, sizeof(local_packs));
    char *id0 = oc_arena_strdup(&a, "ts-js");
    char *g0 = oc_arena_strdup(&a, "javascript");
    char *e0 = oc_arena_strdup(&a, ".js");
    char *id1 = oc_arena_strdup(&a, "python");
    char *g1 = oc_arena_strdup(&a, "python");
    char *e1 = oc_arena_strdup(&a, ".py");
    local_packs[0].id = id0;
    local_packs[0].grammar = g0;
    local_packs[0].extensions = &e0;
    local_packs[0].n_extensions = 1;
    local_packs[1].id = id1;
    local_packs[1].grammar = g1;
    local_packs[1].extensions = &e1;
    local_packs[1].n_extensions = 1;
    oc_pack_set ps;
    ps.arena = &a;
    ps.packs = local_packs;
    ps.n_packs = 2;
    ps.cap_packs = 2;

    const char *roots[1] = {root};
    oc_path_vec found;
    oc_paths_init(&found);
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_discover_files(roots, 1, &ps, &a, &found, NULL, &st) == OC_OK);
    /* Discovery is pack-agnostic: all regular files under non-ignored,
     * non-hidden paths (a.js b.py notes.md sub/c.js sub/deep/d.py). */
    CHECK(found.n == 5);
    if (found.n == 5) {
        /* Paths are rooted at the tmp dir; check suffixes. */
        CHECK_STR_CONTAINS(found.items[0], "/a.js");
        CHECK_STR_CONTAINS(found.items[1], "/b.py");
        CHECK_STR_CONTAINS(found.items[2], "/notes.md");
        CHECK_STR_CONTAINS(found.items[3], "/sub/c.js");
        CHECK_STR_CONTAINS(found.items[4], "/sub/deep/d.py");
    }

    /* Single-file root. */
    char single[512];
    snprintf(single, sizeof(single), "%s/a.js", root);
    oc_path_vec one;
    oc_paths_init(&one);
    const char *one_roots[1] = {single};
    CHECK(oc_discover_files(one_roots, 1, &ps, &a, &one, NULL, &st) == OC_OK);
    CHECK(one.n == 1);

    /* Missing root is a hard error. */
    oc_path_vec none;
    oc_paths_init(&none);
    const char *bad_roots[1] = {"/nonexistent-root-xyz"};
    CHECK(oc_discover_files(bad_roots, 1, &ps, &a, &none, NULL, &st) == OC_ERR_IO);

    oc_paths_destroy(&found);
    oc_paths_destroy(&one);
    oc_paths_destroy(&none);
    oc_arena_destroy(&a);
    TEST_SUMMARY();
}
