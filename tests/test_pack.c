#include "oc/pack.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return;
    }
    fputs(content, f);
    fclose(f);
}

int main(void) {
    char tmpl[] = "/tmp/oc_pack_test_XXXXXX";
    char *root = mkdtemp(tmpl);
    CHECK(root != NULL);
    if (!root)
        return 1;

    char dir_js[256], dir_py[256];
    snprintf(dir_js, sizeof(dir_js), "%s/ts-js", root);
    snprintf(dir_py, sizeof(dir_py), "%s/python", root);
    mkdir(dir_js, 0755);
    mkdir(dir_py, 0755);

    char manifest_js[300], manifest_py[300];
    snprintf(manifest_js, sizeof(manifest_js), "%s/pack.json", dir_js);
    snprintf(manifest_py, sizeof(manifest_py), "%s/pack.json", dir_py);
    write_file(manifest_js, "{\"schema\":\"omnicheck.pack/v0\",\"id\":\"ts-js\","
                            "\"display_name\":\"JavaScript\",\"version\":\"0.1.0\","
                            "\"grammar\":\"javascript\",\"extensions\":[\".js\",\".mjs\"],"
                            "\"entry_points\":[\"index.js\"],"
                            "\"enabled_passes\":[\"OC001\",\"OC002\"]}");
    write_file(manifest_py, "{\"schema\":\"omnicheck.pack/v0\",\"id\":\"python\","
                            "\"display_name\":\"Python 3\",\"version\":\"0.1.0\","
                            "\"grammar\":\"python\",\"extensions\":[\".py\"],"
                            "\"enabled_passes\":[\"OC001\"]}");

    oc_arena a;
    oc_arena_init(&a);
    oc_pack_set ps;
    oc_packs_init(&ps, &a);
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_packs_load_dir(&ps, root, &st) == OC_OK);
    CHECK(ps.n_packs == 2);

    const oc_pack *js = oc_packs_find(&ps, "ts-js");
    const oc_pack *py = oc_packs_find(&ps, "python");
    CHECK(js && py);
    CHECK_STR_EQ(js->grammar, "javascript");
    CHECK(js->n_extensions == 2);
    CHECK_STR_EQ(js->extensions[0], ".js");
    CHECK_STR_EQ(oc_json_as_str((const oc_json *)py->grammar, py->grammar), "python");

    /* Extension matching (case-insensitive). */
    CHECK(oc_packs_match_path(&ps, "app/foo.JS") == js);
    CHECK(oc_packs_match_path(&ps, "app/x.mjs") == js);
    CHECK(oc_packs_match_path(&ps, "app/main.py") == py);
    CHECK(oc_packs_match_path(&ps, "app/notes.md") == NULL);
    CHECK(oc_pack_has_pass(js, "OC002"));
    CHECK(!oc_pack_has_pass(py, "OC002"));

    /* Pack JSON dump parses back. */
    oc_jsonw w;
    oc_jsonw_init(&w);
    CHECK(oc_packs_write_json(&ps, &w));
    oc_status_clear(&st);
    oc_json *dump = oc_json_parse(&a, w.buf.data, w.buf.len, &st);
    CHECK(dump != NULL && oc_json_size(dump) == 2);
    oc_jsonw_destroy(&w);

    oc_packs_destroy(&ps); /* free the loaded array before reuse */
    /* Invalid packs are rejected with precise messages. */
    write_file(manifest_py, "{\"schema\":\"omnicheck.pack/v0\",\"id\":\"python\","
                            "\"display_name\":\"Python 3\",\"version\":\"0.1.0\","
                            "\"grammar\":\"python\",\"extensions\":[]}");
    oc_status_clear(&st);
    CHECK(oc_packs_load_dir(&ps, root, &st) == OC_ERR_PACK);
    CHECK_STR_CONTAINS(st.msg, "extensions");

    write_file(manifest_py, "{\"schema\":\"old\",\"id\":\"python\"}");
    oc_status_clear(&st);
    CHECK(oc_packs_load_dir(&ps, root, &st) == OC_ERR_PACK);
    CHECK_STR_CONTAINS(st.msg, "schema");

    /* Missing pack directory. */
    oc_status_clear(&st);
    CHECK(oc_packs_load_dir(&ps, "/nonexistent-dir-xyz", &st) == OC_ERR_PACK);
    oc_packs_destroy(&ps);

    oc_arena_destroy(&a);
    TEST_SUMMARY();
}
