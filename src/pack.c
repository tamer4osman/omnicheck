#include "oc/pack.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "oc/common.h"
#include "oc/parser.h"

void oc_packs_init(oc_pack_set *ps, oc_arena *arena) {
    ps->arena = arena;
    ps->packs = NULL;
    ps->n_packs = 0;
    ps->cap_packs = 0;
}

void oc_packs_destroy(oc_pack_set *ps) {
    free(ps->packs);
    ps->packs = NULL;
    ps->n_packs = 0;
    ps->cap_packs = 0;
}

static int packs_reserve(oc_pack_set *ps) {
    if (ps->n_packs < ps->cap_packs)
        return 1;
    size_t cap = ps->cap_packs ? ps->cap_packs * 2 : 4;
    oc_pack *np = (oc_pack *)realloc(ps->packs, cap * sizeof(oc_pack));
    if (!np)
        return 0;
    ps->packs = np;
    ps->cap_packs = cap;
    return 1;
}

static const char *json_str_field(oc_arena *a, const oc_json *obj, const char *key, char **dest,
                                  int required, oc_status *st, const char *src) {
    const oc_json *v = oc_json_get(obj, key);
    if (!v || v->type != OC_JSON_STR) {
        if (required) {
            oc_status_set(st, OC_ERR_PACK, "pack '%s': missing string field '%s'", src, key);
            return NULL;
        }
        return "";
    }
    *dest = oc_arena_strdup(a, v->str);
    if (!*dest) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory loading pack '%s'", src);
        return NULL;
    }
    return *dest;
}

static int json_str_array(oc_arena *a, const oc_json *obj, const char *key, char ***dest, size_t *n,
                          int required, oc_status *st, const char *src) {
    const oc_json *v = oc_json_get(obj, key);
    if (!v) {
        if (required) {
            oc_status_set(st, OC_ERR_PACK, "pack '%s': missing array field '%s'", src, key);
            return 0;
        }
        *dest = NULL;
        *n = 0;
        return 1;
    }
    if (v->type != OC_JSON_ARR) {
        oc_status_set(st, OC_ERR_PACK, "pack '%s': field '%s' must be an array", src, key);
        return 0;
    }
    size_t count = v->n_items;
    *n = count;
    if (count == 0) {
        *dest = NULL;
        return 1;
    }
    char **arr = (char **)oc_arena_alloc(a, count * sizeof(char *));
    if (!arr) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory loading pack '%s'", src);
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        const oc_json *item = v->items[i];
        if (!item || item->type != OC_JSON_STR) {
            oc_status_set(st, OC_ERR_PACK, "pack '%s': field '%s' must contain only strings", src,
                          key);
            return 0;
        }
        arr[i] = oc_arena_strdup(a, item->str);
        if (!arr[i]) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory loading pack '%s'", src);
            return 0;
        }
    }
    *dest = arr;
    return 1;
}

static oc_code pack_load_file(oc_pack_set *ps, const char *path, oc_status *st) {
    char *data = NULL;
    size_t len = 0;
    oc_code rc = oc_read_file(path, &data, &len, st);
    if (rc != OC_OK)
        return rc;

    oc_json *root = oc_json_parse(ps->arena, data, len, st);
    free(data);
    if (!root) {
        oc_status_set(st, OC_ERR_PACK, "pack '%s': %s", path,
                      st->msg[0] ? st->msg : "invalid JSON");
        return OC_ERR_PACK;
    }
    if (root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_PACK, "pack '%s': top level must be an object", path);
        return OC_ERR_PACK;
    }

    const oc_json *schema = oc_json_get(root, "schema");
    if (!schema || schema->type != OC_JSON_STR || strcmp(schema->str, OC_PACK_SCHEMA) != 0) {
        oc_status_set(st, OC_ERR_PACK, "pack '%s': schema must be \"%s\"", path, OC_PACK_SCHEMA);
        return OC_ERR_PACK;
    }

    if (!packs_reserve(ps)) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory loading pack '%s'", path);
        return OC_ERR_ALLOC;
    }
    oc_pack *p = &ps->packs[ps->n_packs];
    memset(p, 0, sizeof(*p));
    p->source_path = oc_arena_strdup(ps->arena, path);

    if (!json_str_field(ps->arena, root, "id", &p->id, 1, st, path))
        return OC_ERR_PACK;
    if (p->id[0] == '\0') {
        oc_status_set(st, OC_ERR_PACK, "pack '%s': 'id' must not be empty", path);
        return OC_ERR_PACK;
    }
    if (!json_str_field(ps->arena, root, "display_name", &p->display_name, 1, st, path))
        return OC_ERR_PACK;
    if (!json_str_field(ps->arena, root, "grammar", &p->grammar, 1, st, path))
        return OC_ERR_PACK;
    if (!json_str_field(ps->arena, root, "version", &p->version, 1, st, path))
        return OC_ERR_PACK;
    if (!json_str_field(ps->arena, root, "notes", &p->notes, 0, st, path))
        return OC_ERR_PACK;
    if (!json_str_array(ps->arena, root, "extensions", &p->extensions, &p->n_extensions, 1, st,
                        path))
        return OC_ERR_PACK;
    if (p->n_extensions == 0) {
        oc_status_set(st, OC_ERR_PACK, "pack '%s': 'extensions' must list at least one entry",
                      path);
        return OC_ERR_PACK;
    }
    for (size_t i = 0; i < p->n_extensions; i++) {
        if (p->extensions[i][0] != '.') {
            oc_status_set(st, OC_ERR_PACK, "pack '%s': extension '%s' must start with '.'", path,
                          p->extensions[i]);
            return OC_ERR_PACK;
        }
    }
    if (!json_str_array(ps->arena, root, "entry_points", &p->entry_points, &p->n_entry_points, 0,
                        st, path))
        return OC_ERR_PACK;
    if (!json_str_array(ps->arena, root, "enabled_passes", &p->passes, &p->n_passes, 0, st, path))
        return OC_ERR_PACK;

    /* Declared build context (SPRINT-14, docs/LANGUAGES.md §Go.8): the
     * deterministic analysis target for build-constraint evaluation -- pack
     * data, not the analysis host. Absent = no constraint evaluation (the
     * pack opts in); present = goos/goarch REQUIRED (fail loud, never
     * guess a target), compiler defaults to "gc", cgo to 0, tags to none. */
    const oc_json *bctx = oc_json_get(root, "build_context");
    if (bctx && bctx->type == OC_JSON_OBJ) {
        if (!json_str_field(ps->arena, bctx, "goos", &p->build_goos, 1, st, path))
            return OC_ERR_PACK;
        if (!json_str_field(ps->arena, bctx, "goarch", &p->build_goarch, 1, st, path))
            return OC_ERR_PACK;
        if (!json_str_field(ps->arena, bctx, "compiler", &p->build_compiler, 0, st, path))
            return OC_ERR_PACK;
        if (!p->build_compiler) {
            p->build_compiler = (char *)oc_arena_strdup(ps->arena, "gc");
            if (!p->build_compiler) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory loading pack '%s'", path);
                return OC_ERR_ALLOC;
            }
        }
        p->build_cgo = oc_json_as_bool(oc_json_get(bctx, "cgo"), 0);
        if (!json_str_array(ps->arena, bctx, "tags", &p->build_tags, &p->n_build_tags, 0, st, path))
            return OC_ERR_PACK;
    }

    ps->n_packs++;
    oc_status_clear(st);
    return OC_OK;
}

oc_code oc_packs_load_dir(oc_pack_set *ps, const char *dir, oc_status *st) {
    DIR *d = opendir(dir);
    if (!d) {
        oc_status_set(st, OC_ERR_PACK, "cannot open packs directory '%s'", dir);
        return OC_ERR_PACK;
    }
    struct dirent *ent;
    oc_code rc = OC_OK;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        char sub[1024];
        if ((size_t)snprintf(sub, sizeof(sub), "%s/%s", dir, ent->d_name) >= sizeof(sub))
            continue;
        struct stat sb;
        if (stat(sub, &sb) != 0 || !S_ISDIR(sb.st_mode))
            continue;
        char manifest[1200];
        if ((size_t)snprintf(manifest, sizeof(manifest), "%s/pack.json", sub) >= sizeof(manifest))
            continue;
        struct stat mb;
        if (stat(manifest, &mb) != 0 || !S_ISREG(mb.st_mode))
            continue;
        rc = pack_load_file(ps, manifest, st);
        if (rc != OC_OK)
            break;
    }
    closedir(d);
    if (rc != OC_OK)
        return rc;
    if (ps->n_packs == 0) {
        oc_status_set(st, OC_ERR_PACK,
                      "no packs found under '%s' (expected <id>/pack.json "
                      "subdirectories)",
                      dir);
        return OC_ERR_PACK;
    }
    return OC_OK;
}

const oc_pack *oc_packs_find(const oc_pack_set *ps, const char *id) {
    for (size_t i = 0; i < ps->n_packs; i++) {
        if (strcmp(ps->packs[i].id, id) == 0)
            return &ps->packs[i];
    }
    return NULL;
}

const oc_pack *oc_packs_match_path(const oc_pack_set *ps, const char *path) {
    for (size_t i = 0; i < ps->n_packs; i++) {
        const oc_pack *p = &ps->packs[i];
        for (size_t j = 0; j < p->n_extensions; j++) {
            if (oc_path_has_ext(path, p->extensions[j]))
                return p;
        }
    }
    return NULL;
}

int oc_pack_has_pass(const oc_pack *p, const char *pass_id) {
    for (size_t i = 0; i < p->n_passes; i++) {
        if (strcmp(p->passes[i], pass_id) == 0)
            return 1;
    }
    return 0;
}

int oc_packs_write_json(const oc_pack_set *ps, oc_jsonw *w) {
    oc_jsonw_arr_begin(w);
    for (size_t i = 0; i < ps->n_packs; i++) {
        const oc_pack *p = &ps->packs[i];
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "id");
        oc_jsonw_str(w, p->id);
        oc_jsonw_key(w, "display_name");
        oc_jsonw_str(w, p->display_name);
        oc_jsonw_key(w, "grammar");
        oc_jsonw_str(w, p->grammar);
        oc_jsonw_key(w, "version");
        oc_jsonw_str(w, p->version);
        oc_jsonw_key(w, "extensions");
        oc_jsonw_arr_begin(w);
        for (size_t j = 0; j < p->n_extensions; j++)
            oc_jsonw_str(w, p->extensions[j]);
        oc_jsonw_arr_end(w);
        oc_jsonw_key(w, "entry_points");
        oc_jsonw_arr_begin(w);
        for (size_t j = 0; j < p->n_entry_points; j++)
            oc_jsonw_str(w, p->entry_points[j]);
        oc_jsonw_arr_end(w);
        oc_jsonw_key(w, "enabled_passes");
        oc_jsonw_arr_begin(w);
        for (size_t j = 0; j < p->n_passes; j++)
            oc_jsonw_str(w, p->passes[j]);
        oc_jsonw_arr_end(w);
        oc_jsonw_key(w, "notes");
        oc_jsonw_str(w, p->notes);
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_arr_end(w);
    return 1;
}

/* SPRINT-21: the pack routing treatment. See include/oc/pack.h for the
 * contract. Extension candidates are collected FIRST (match order is
 * readdir order -- never a semantic), and only the .h overlap between
 * the `c` and `cpp` packs is routed through the sniff + root-default
 * treatment; every other shape stays the plain extension match. */
const oc_pack *oc_packs_route_source(const oc_pack_set *ps, const char *path, const char *src,
                                     size_t len, const char *const *discovered,
                                     size_t n_discovered, oc_arena *a,
                                     oc_route_reason *out_reason) {
    (void)a;
    if (out_reason)
        *out_reason = OC_ROUTE_EXT;
    if (!ps || ps->n_packs == 0)
        return NULL;
    /* collect every pack whose extension list claims this path */
    const oc_pack *cand[16];
    size_t n_cand = 0;
    for (size_t i = 0; i < ps->n_packs && n_cand < 16; i++) {
        const oc_pack *p = &ps->packs[i];
        for (size_t j = 0; j < p->n_extensions; j++) {
            if (oc_path_has_ext(path, p->extensions[j])) {
                cand[n_cand++] = p;
                break;
            }
        }
    }
    if (n_cand == 0)
        return NULL;
    if (n_cand == 1)
        return cand[0];
    /* ambiguous: the ONLY configured overlap is .h between c and cpp */
    const oc_pack *c_pack = oc_packs_find(ps, "c");
    const oc_pack *cpp_pack = oc_packs_find(ps, "cpp");
    if (!c_pack || !cpp_pack || !oc_path_has_ext(path, ".h"))
        return cand[0]; /* not the documented overlap: first match (the v0 shape) */

    /* 1. the content sniff (parse facts only; NULL source degrades) */
    oc_sniff_verdict verdict = src ? oc_parser_sniff_header(src, len) : OC_SNIFF_INCONCLUSIVE;
    if (verdict == OC_SNIFF_CPP_POSITIVE) {
        if (out_reason)
            *out_reason = OC_ROUTE_H_SNIFF_CPP;
        return cpp_pack;
    }
    if (verdict == OC_SNIFF_C_POSITIVE) {
        if (out_reason)
            *out_reason = OC_ROUTE_H_SNIFF_C;
        return c_pack;
    }

    /* 2. the project-root default: cpp iff the discovered set carries at
     * least one NON-.h cpp-pack extension (the project uses C++) */
    for (size_t j = 0; j < cpp_pack->n_extensions; j++) {
        if (strcmp(cpp_pack->extensions[j], ".h") == 0)
            continue; /* the ambiguous extension never self-asserts */
        for (size_t i = 0; i < n_discovered; i++) {
            if (oc_path_has_ext(discovered[i], cpp_pack->extensions[j])) {
                if (out_reason)
                    *out_reason = OC_ROUTE_H_DEFAULT_CPP;
                return cpp_pack;
            }
        }
    }
    if (out_reason)
        *out_reason = OC_ROUTE_H_DEFAULT_C;
    return c_pack;
}
