#include "oc/supply.h"

#include "pathutil.h" /* oc_path_dirname (go.mod module-root licence probe) */
#include <stdio.h>
#include <stdlib.h>

/*
 * Supply-chain tier (WBS 4.2). See supply.h for the contract. Everything
 * here is deterministic: fixed manifest set, fixed pattern list, fixed
 * comparison rules, vendored snapshot data with a visible date.
 */
#define OC_SUPPLY_SECRET_RULE "OC011"

static const char *kind_name(oc_manifest_kind k) {
    switch (k) {
    case OC_MANIFEST_PACKAGE_JSON:
        return "package.json";
    case OC_MANIFEST_PYPROJECT:
        return "pyproject.toml";
    case OC_MANIFEST_REQUIREMENTS:
        return "requirements.txt";
    case OC_MANIFEST_POM:
        return "pom.xml";
    case OC_MANIFEST_GOMOD:
        return "go.mod";
    case OC_MANIFEST_CSPROJ:
        return ".csproj";
    case OC_MANIFEST_GRADLE:
        return "build.gradle";
    case OC_MANIFEST_PKGS_CONFIG:
        return "packages.config";
    case OC_MANIFEST_PKGS_LOCK:
        return "packages.lock.json";
    case OC_MANIFEST_CARGO_TOML:
        return "Cargo.toml";
    case OC_MANIFEST_CARGO_LOCK:
        return "Cargo.lock";
    case OC_MANIFEST_GEMFILE:
        return "Gemfile";
    case OC_MANIFEST_GEMFILE_LOCK:
        return "Gemfile.lock";
    case OC_MANIFEST_COMPOSER_JSON:
        return "composer.json";
    case OC_MANIFEST_COMPOSER_LOCK:
        return "composer.lock";
    }
    return "?";
}

const char *oc_manifest_kind_str(oc_manifest_kind k) {
    return kind_name(k);
}

const char *oc_ecosystem_str(oc_ecosystem e) {
    switch (e) {
    case OC_ECO_NPM:
        return "npm";
    case OC_ECO_PYPI:
        return "PyPI";
    case OC_ECO_GO:
        return "Go";
    case OC_ECO_MAVEN:
        return "Maven";
    case OC_ECO_NUGET:
        return "NuGet";
    case OC_ECO_CRATES:
        return "crates.io";
    case OC_ECO_RUBYGEMS:
        return "rubygems";
    case OC_ECO_PACKAGIST:
        return "packagist";
    case OC_ECO_NONE:
        return "none";
    }
    return "?";
}

/* ------------------------------------------------------------ manifests */

void oc_manifests_init(oc_manifest_vec *v) {
    v->items = NULL;
    v->n = 0;
    v->cap = 0;
}

void oc_manifests_destroy(oc_manifest_vec *v) {
    for (size_t i = 0; i < v->n; i++)
        free(v->items[i].deps); /* each manifest owns its dep array */
    free(v->items);
    oc_manifests_init(v);
}

static int manifest_push(oc_manifest_vec *v, oc_manifest m) {
    if (v->n == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 4;
        oc_manifest *grown = (oc_manifest *)realloc(v->items, ncap * sizeof(*grown));
        if (!grown)
            return 0;
        v->items = grown;
        v->cap = ncap;
    }
    v->items[v->n++] = m;
    return 1;
}

static int dep_push(oc_manifest *m, oc_arena *a, const char *name, const char *version, size_t line,
                    int pinned) {
    if (m->n_deps == m->cap_deps) {
        size_t ncap = m->cap_deps ? m->cap_deps * 2 : 8;
        oc_dep *grown = (oc_dep *)realloc(m->deps, ncap * sizeof(*grown));
        if (!grown)
            return 0;
        m->deps = grown;
        m->cap_deps = ncap;
    }
    oc_dep *d = &m->deps[m->n_deps++];
    d->name = oc_arena_strdup(a, name);
    d->version = version ? oc_arena_strdup(a, version) : NULL;
    d->line = line;
    d->pinned = pinned;
    return d->name != NULL && (version == NULL || d->version != NULL);
}

/* A package.json dependency specifier is honestly matchable only when
 * it is an exact pin: digits and dots (optionally a leading 'v'). Range
 * specifiers (^ ~ * x >= < || -) can name many versions. */
static int npm_exact_pin(const char *spec) {
    size_t i = 0;
    if (spec[i] == 'v')
        i++;
    if (spec[i] == '\0')
        return 0;
    for (; spec[i]; i++) {
        char c = spec[i];
        if (!((c >= '0' && c <= '9') || c == '.'))
            return 0;
    }
    return 1;
}

static oc_code load_package_json(oc_manifest *m, const char *path, const char *data, size_t len,
                                 oc_arena *a, oc_status *st) {
    oc_json *root = oc_json_parse(a, data, len, st);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "manifest '%s' is not a JSON object", path);
        return OC_ERR_USAGE;
    }
    const oc_json *lic = oc_json_get(root, "license");
    if (lic && lic->type == OC_JSON_STR && lic->str[0])
        m->license = lic->str;
    for (size_t s = 0; s < 2; s++) { /* dependencies, then devDependencies */
        const char *section = s == 0 ? "dependencies" : "devDependencies";
        const oc_json *deps = oc_json_get(root, section);
        if (!deps || deps->type != OC_JSON_OBJ)
            continue;
        for (size_t i = 0; i < deps->n_fields; i++) {
            const char *name = deps->keys[i];
            const char *spec = oc_json_as_str(deps->vals[i], "");
            if (!npm_exact_pin(spec))
                continue; /* ranges: never guessed (see supply.h) */
            if (!dep_push(m, a, name, spec, 1, 1)) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                return OC_ERR_ALLOC;
            }
        }
    }
    return OC_OK;
}

/* pyproject.toml: minimal line scan for the two fields the licence rule
 * and the dependency rule need. This is NOT a TOML parser; it is an
 * honest bounded reader for [project] license = "..." and dependencies
 * from `dependencies = [...]` on one logical line. Anything ambiguous is
 * skipped (recorded in docs), never guessed. */
static oc_code load_pyproject(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                              oc_status *st) {
    m->kind = OC_MANIFEST_PYPROJECT;
    m->eco = OC_ECO_PYPI;
    (void)path;
    (void)a;
    int in_project = 0;
    const char *p = data;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        /* strip a trailing \r */
        if (linelen > 0 && p[linelen - 1] == '\r')
            linelen--;
        const char *line = p;
        /* leading whitespace */
        size_t i = 0;
        while (i < linelen && (line[i] == ' ' || line[i] == '\t'))
            i++;
        if (i < linelen && line[i] == '[') {
            in_project = (linelen - i >= 9 && memcmp(line + i, "[project]", 9) == 0);
        } else if (in_project) {
            if (!m->license && linelen - i >= 9 && memcmp(line + i, "license =", 9) == 0) {
                const char *q = memchr(line + i, '"', linelen - i);
                if (q) {
                    const char *q2 = memchr(q + 1, '"', (size_t)(line + linelen - q - 1));
                    if (q2 && q2 > q + 1)
                        m->license = oc_arena_strndup(a, q + 1, (size_t)(q2 - q - 1));
                }
            } else if (linelen - i >= 14 && memcmp(line + i, "dependencies =", 14) == 0) {
                /* dependencies = ["name==1.2.3", 'x>=2'] */
                const char *q = memchr(line + i, '[', linelen - i);
                const char *end = memchr(line + i, ']', linelen - i);
                if (q && end && end > q) {
                    const char *c = q + 1;
                    while (c < end) {
                        char quote = 0;
                        while (c < end && *c != '"' && *c != '\'' && *c != ']')
                            c++;
                        if (c >= end)
                            break;
                        quote = *c++;
                        const char *start = c;
                        while (c < end && *c != quote)
                            c++;
                        if (c > start) {
                            /* PEP 508 name[extras](op version): keep the
                             * name; pinned only with exact '=='. */
                            char namebuf[128];
                            size_t nlen = 0;
                            const char *s = start;
                            while (s < c && nlen < sizeof(namebuf) - 1) {
                                if (*s == '[' || *s == '(' || *s == '=' || *s == '>' || *s == '<' ||
                                    *s == '!' || *s == '~' || *s == ' ' || *s == ';')
                                    break;
                                namebuf[nlen++] = *s++;
                            }
                            namebuf[nlen] = '\0';
                            int pinned = ((size_t)(c - start) >= nlen + 2 && start[nlen] == '=' &&
                                          start[nlen + 1] == '=');
                            const char *ver = NULL;
                            if (pinned) {
                                const char *vs = start + nlen + 2;
                                size_t vlen = (size_t)(c - vs);
                                char vbuf[64];
                                if (vlen < sizeof(vbuf)) {
                                    memcpy(vbuf, vs, vlen);
                                    vbuf[vlen] = '\0';
                                    ver = oc_arena_strdup(a, vbuf);
                                }
                            }
                            if (nlen > 0 &&
                                !dep_push(m, a, namebuf, ver, 1, pinned && ver != NULL)) {
                                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                                return OC_ERR_ALLOC;
                            }
                        }
                        if (c < end)
                            c++; /* past closing quote */
                    }
                }
            }
        }
        p = eol ? eol + 1 : NULL;
    }
    return OC_OK;
}

/* requirements.txt: one requirement per line, 'name==version' is a pin.
 * Lines: '#' comments, '-r' includes, blanks are skipped. */
static oc_code load_requirements(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                                 oc_status *st) {
    m->kind = OC_MANIFEST_REQUIREMENTS;
    m->eco = OC_ECO_PYPI;
    (void)path;
    (void)a;
    size_t lineno = 0;
    const char *p = data;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        if (linelen > 0 && p[linelen - 1] == '\r')
            linelen--;
        lineno++;
        size_t i = 0;
        while (i < linelen && (p[i] == ' ' || p[i] == '\t'))
            i++;
        if (i < linelen && p[i] != '#' && p[i] != '-') {
            const char *s = p + i;
            size_t nlen = 0;
            while (i + nlen < linelen) {
                char c = p[i + nlen];
                if (c == '=' || c == '>' || c == '<' || c == '!' || c == '~' || c == ' ' ||
                    c == ';' || c == '[')
                    break;
                nlen++;
            }
            if (nlen > 0 && nlen < 128) {
                char namebuf[128];
                memcpy(namebuf, s, nlen);
                namebuf[nlen] = '\0';
                int pinned =
                    (i + nlen + 1 < linelen && p[i + nlen] == '=' && p[i + nlen + 1] == '=');
                const char *ver = NULL;
                if (pinned) {
                    size_t vs = i + nlen + 2;
                    size_t vlen = 0;
                    while (vs + vlen < linelen) {
                        char c = p[vs + vlen];
                        if (c == ' ' || c == ';' || c == ',')
                            break;
                        vlen++;
                    }
                    if (vlen > 0 && vlen < 64) {
                        char vbuf[64];
                        memcpy(vbuf, p + vs, vlen);
                        vbuf[vlen] = '\0';
                        ver = oc_arena_strdup(a, vbuf);
                    }
                    pinned = ver != NULL;
                }
                if (!dep_push(m, a, namebuf, ver, lineno, pinned && ver != NULL)) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                    return OC_ERR_ALLOC;
                }
            }
        }
        p = eol ? eol + 1 : NULL;
    }
    return OC_OK;
}

/* go.mod (SPRINT-11, docs/LANGUAGES.md §Go.6): a minimal line reader
 * for the two things the supply tier needs. This is NOT a go.mod parser;
 * it is an honest bounded reader: the `module` clause names the module
 * (the go.mod directory is its root), `require` entries (single and
 * grouped; `// indirect` included -- they are real build inputs) carry
 * an exact semantic version, so every require is a pin; version ranges
 * do not exist in go.mod. The go/toolchain directives and the
 * exclude/replace/retract statements are not dependencies and are
 * skipped. Comments (`//`) and blanks are skipped; anything ambiguous
 * is skipped (recorded here), never guessed. */
static void gomod_strip_comment(char *s, size_t *len) {
    /* the COMMENT marker is a double slash; module paths contain single
     * slashes ("golang.org/x/crypto"), so scan for "//" as a pair */
    for (size_t i = 0; i + 1 < *len; i++) {
        if (s[i] == '/' && s[i + 1] == '/') {
            *len = i;
            return;
        }
    }
}

static void gomod_tokens(char *s, size_t len, char *tok[3], size_t *n_tok) {
    size_t i = 0, n = 0;
    while (i < len && n < 3) {
        while (i < len && (s[i] == ' ' || s[i] == '\t'))
            i++;
        if (i >= len)
            break;
        tok[n++] = s + i;
        while (i < len && s[i] != ' ' && s[i] != '\t')
            i++;
        s[i] = '\0'; /* safe: the line buffer is writable and mutable */
        i++;
    }
    *n_tok = n;
}

static oc_code load_gomod(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                          oc_status *st) {
    m->kind = OC_MANIFEST_GOMOD;
    m->eco = OC_ECO_GO;
    (void)path;
    /* arena-owned mutable line buffer (tokens are NUL-terminated in place) */
    char *buf = oc_arena_alloc(a, strlen(data) + 1);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing go.mod line buffer");
        return OC_ERR_ALLOC;
    }
    size_t lineno = 0;
    int in_require = 0;
    const char *p = data;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t linelen = eol ? (size_t)(eol - p) : strlen(p);
        if (linelen > 0 && p[linelen - 1] == '\r')
            linelen--;
        lineno++;
        memcpy(buf, p, linelen);
        buf[linelen] = '\0';
        gomod_strip_comment(buf, &linelen);
        size_t i = 0;
        while (i < linelen && (buf[i] == ' ' || buf[i] == '\t'))
            i++;
        char *tok[3];
        size_t n_tok = 0;
        gomod_tokens(buf + i, linelen - i, tok, &n_tok);
        if (n_tok > 0) {
            if (strcmp(tok[0], "require") == 0) {
                if (n_tok >= 2 && strcmp(tok[1], "(") == 0) {
                    in_require = 1; /* grouped block: entries follow */
                } else if (n_tok >= 3) {
                    if (!dep_push(m, a, tok[1], tok[2], lineno, 1)) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                        return OC_ERR_ALLOC;
                    }
                }
            } else if (in_require && strcmp(tok[0], ")") != 0 && n_tok >= 2) {
                if (!dep_push(m, a, tok[0], tok[1], lineno, 1)) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                    return OC_ERR_ALLOC;
                }
            } else if (strcmp(tok[0], ")") == 0) {
                in_require = 0;
            }
        }
        p = eol ? eol + 1 : NULL;
    }
    return OC_OK;
}

/* OC010 licence detection for go.mod (docs/LANGUAGES.md §Go.6, SPRINT-11
 * amendment): go.mod carries NO licence field. The module clause
 * identifies the module root -- the go.mod's own directory -- and a
 * licence FILE (basename exactly LICENSE, LICENCE, COPYING or NOTICE,
 * compared case-insensitively) directly inside that root, among the
 * DISCOVERED files, decides: found -> the licence is detected (the
 * text is never parsed); absent -> m->license stays NULL and OC010
 * warns, same as the npm/pyproject manifests. In-universe: the probe
 * reads the already-discovered file list, never the filesystem. */
static void gomod_detect_licence(oc_manifest *m, oc_arena *a, const char *const *files,
                                 size_t n_files) {
    static const char *const LICENCE_BASES[] = {"license", "licence", "copying", "notice"};
    char root[1024];
    oc_path_dirname(m->path, root, sizeof(root));
    size_t rl = strlen(root);
    for (size_t i = 0; i < n_files; i++) {
        if (strncmp(files[i], root, rl) != 0 || files[i][rl] != '/')
            continue;
        const char *rest = files[i] + rl + 1;
        if (strchr(rest, '/') != NULL)
            continue; /* not directly inside the module root */
        char base[128];
        size_t bl = strlen(rest);
        if (bl == 0 || bl >= sizeof(base))
            continue;
        for (size_t c = 0; c <= bl; c++)
            base[c] = (char)((rest[c] >= 'A' && rest[c] <= 'Z') ? rest[c] - 'A' + 'a' : rest[c]);
        for (size_t k = 0; k < sizeof(LICENCE_BASES) / sizeof(LICENCE_BASES[0]); k++) {
            if (strcmp(base, LICENCE_BASES[k]) != 0)
                continue;
            m->license = oc_arena_sprintf(a, "module-root file %s", rest);
            return;
        }
    }
}

/* pom.xml (SPRINT-11, docs/LANGUAGES.md §Java.6): a minimal, honest XML
 * scanner for exactly the three facts the supply tier needs -- the
 * project licence clause (OC010) and the dependency coordinates with
 * their versions (OC009, OSV ecosystem Maven, names are the OSV Maven
 * convention "group:artifact"). This is NOT an XML parser; it is a
 * bounded element-stack walk in the pyproject.toml precedent. Scope and
 * never-guess rules, all pinned by the fixtures:
 *   - only the ROOT-LEVEL <dependencies> (a direct child of <project>)
 *     is read; <dependencyManagement>, profile, build and plugin
 *     dependency blocks are NOT dependencies of the project and are
 *     skipped (recorded descope);
 *   - a dependency is pinned only when <version> names ONE usable
 *     version: no property references (${...} is never substituted --
 *     guessing the property value would be fabrication), no Maven
 *     ranges ([..] (..) or anything with a comma), no LATEST/RELEASE;
 *   - a dependency with no <version> is unpinned (inherited from a
 *     parent pom or dependencyManagement -- out of scope) and is
 *     reported by OC009 as warn, never matched;
 *   - the licence is the first <name> inside <licenses><license>
 *     ("the project clause"); licence inheritance from a parent pom is
 *     out of scope and never guessed.
 * Line anchoring: the scanner does not track line numbers, so manifest
 * findings anchor at line 1 (the package.json precedent -- the report
 * must not guess). */
#define OC_POM_MAX_DEPTH 64
#define OC_POM_NAME_CAP  48

typedef struct {
    char name[OC_POM_NAME_CAP];
} pom_frame;

/* True when the element name is one of the simple-text facts the scanner
 * captures. */
static int pom_is_capture_name(const char *n, size_t len) {
    if (len == 7 && memcmp(n, "groupId", 7) == 0)
        return 1;
    if (len == 10 && memcmp(n, "artifactId", 10) == 0)
        return 1;
    if (len == 7 && memcmp(n, "version", 7) == 0)
        return 1;
    if (len == 4 && memcmp(n, "name", 4) == 0)
        return 1;
    return 0;
}

/* A pom <version> is honestly matchable only when it names ONE version:
 * no property references ("${...}" is never substituted), no Maven
 * ranges (anything with [ ] ( ) or a comma), no LATEST/RELEASE aliases. */
static int maven_exact_pin(const char *v) {
    if (!v || v[0] == '\0')
        return 0;
    if (strcmp(v, "LATEST") == 0 || strcmp(v, "RELEASE") == 0)
        return 0;
    for (const char *p = v; *p; p++) {
        if (*p == '$' || *p == '[' || *p == ']' || *p == '(' || *p == ')' || *p == ',')
            return 0;
    }
    return 1;
}

static oc_code load_pom(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                        oc_status *st) {
    m->kind = OC_MANIFEST_POM;
    m->eco = OC_ECO_MAVEN;
    (void)path;
    pom_frame stack[OC_POM_MAX_DEPTH];
    size_t depth = 0;
    int root_project = 0;
    /* dependency capture state */
    int in_dep = 0;
    char gbuf[160] = "", abuf[160] = "", vbuf[160] = "";
    int have_g = 0, have_a = 0, have_v = 0;
    int lic_done = 0;

    const char *p = data;
    while (p && *p) {
        const char *lt = strchr(p, '<');
        if (!lt)
            break;
        const char *c = lt + 1;
        if (*c == '!') { /* comment or DOCTYPE: skip whole construct */
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
        if (*c == '?') { /* processing instruction */
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
        const char *text_start = gt + 1;
        p = gt + 1;
        if (nlen == 0 || nlen >= OC_POM_NAME_CAP)
            continue; /* junk between tags: skipped, honestly */
        char name[OC_POM_NAME_CAP];
        memcpy(name, nstart, nlen);
        name[nlen] = '\0';

        if (closing) {
            if (depth == 0)
                continue;
            depth--;
            if (in_dep && strcmp(name, "dependency") == 0) {
                in_dep = 0;
                if (have_g && have_a) {
                    char depname[324];
                    snprintf(depname, sizeof(depname), "%s:%s", gbuf, abuf);
                    int pinned = have_v && maven_exact_pin(vbuf);
                    if (!dep_push(m, a, depname, pinned ? vbuf : NULL, 1, pinned && have_v)) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                        return OC_ERR_ALLOC;
                    }
                }
                have_g = have_a = have_v = 0;
                gbuf[0] = abuf[0] = vbuf[0] = '\0';
            }
            continue;
        }
        if (!selfclosing) {
            /* opening element: push, then update the scanner state */
            if (depth == 0)
                root_project = (nlen == 7 && memcmp(name, "project", 7) == 0);
            if (depth < OC_POM_MAX_DEPTH) {
                memcpy(stack[depth].name, name, nlen + 1);
                depth++;
            }
            if (depth >= 3 && !lic_done && strcmp(name, "name") == 0 &&
                strcmp(stack[depth - 2].name, "license") == 0 &&
                strcmp(stack[depth - 3].name, "licenses") == 0) {
                /* first <licenses><license><name> text is the licence */
                const char *lt2 = strchr(text_start, '<');
                if (lt2 && lt2 > text_start) {
                    const char *s = text_start;
                    const char *e2 = lt2;
                    while (s < e2 && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
                        s++;
                    while (e2 > s &&
                           (e2[-1] == ' ' || e2[-1] == '\t' || e2[-1] == '\n' || e2[-1] == '\r'))
                        e2--;
                    if (e2 > s) {
                        m->license = oc_arena_strndup(a, s, (size_t)(e2 - s));
                        lic_done = m->license != NULL;
                    }
                }
            }
            if (depth == 2 && root_project && strcmp(name, "dependencies") == 0 &&
                strcmp(stack[depth - 2].name, "project") == 0) {
                /* root-level <project><dependencies>: the only dependency
                 * block v1 reads (dependencyManagement/profiles/build are
                 * not project dependencies -- recorded descope) */
            } else if (depth == 3 && in_dep == 0 && strcmp(name, "dependency") == 0 &&
                       strcmp(stack[depth - 2].name, "dependencies") == 0 &&
                       strcmp(stack[depth - 3].name, "project") == 0) {
                in_dep = 1;
            }
            if (in_dep && depth >= 2 && strcmp(stack[depth - 1].name, name) == 0 &&
                pom_is_capture_name(name, nlen)) {
                /* simple-text element: the bytes up to the next '<' are
                 * the value (groupId/artifactId/version carry no nested
                 * markup in a valid pom) */
                const char *lt2 = strchr(text_start, '<');
                if (lt2 && lt2 > text_start) {
                    const char *s = text_start;
                    const char *e2 = lt2;
                    while (s < e2 && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
                        s++;
                    while (e2 > s &&
                           (e2[-1] == ' ' || e2[-1] == '\t' || e2[-1] == '\n' || e2[-1] == '\r'))
                        e2--;
                    size_t vlen = (size_t)(e2 - s);
                    char *dst = strcmp(name, "groupId") == 0
                                    ? gbuf
                                    : (strcmp(name, "artifactId") == 0 ? abuf : vbuf);
                    int *flag = strcmp(name, "groupId") == 0
                                    ? &have_g
                                    : (strcmp(name, "artifactId") == 0 ? &have_a : &have_v);
                    if (vlen < sizeof(gbuf)) {
                        memcpy(dst, s, vlen);
                        dst[vlen] = '\0';
                        *flag = 1;
                    }
                }
            }
        }
    }
    if (depth != 0 || !root_project) {
        /* the walk must end on the root element's close; anything else is
         * a truncated/foreign file, never silently half-parsed */
        oc_status_set(st, OC_ERR_USAGE, "manifest '%s' is not a complete pom.xml", path);
        return OC_ERR_USAGE;
    }
    return OC_OK;
}

static char *xml_strip_comments(oc_arena *a, const char *data) {
    size_t n = strlen(data);
    char *out = oc_arena_alloc(a, n + 1);
    if (!out)
        return NULL;
    size_t w = 0;
    for (size_t i = 0; i < n;) {
        if (i + 4 <= n && memcmp(data + i, "<!--", 4) == 0) {
            const char *end = strstr(data + i + 4, "-->");
            if (!end)
                break; /* unterminated comment: the rest is dropped, honest */
            i = (size_t)(end - data) + 3;
            continue;
        }
        out[w++] = data[i++];
    }
    out[w] = '\0';
    return out;
}

/* Content of the first <tag>...</tag> block starting at s (NULL when
 * absent or empty). The returned pointer lives in the stripped buffer. */
static const char *xml_block(const char *s, const char *tag, const char **end_out) {
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *p = s;
    while ((p = strstr(p, open)) != NULL) {
        p += strlen(open);
        if (*p == '>' || (*p == ' ' && strchr(p, '>'))) {
            const char *gt = strchr(p, '>');
            if (!gt)
                return NULL;
            const char *content = gt + 1;
            const char *closep = strstr(content, close);
            if (!closep)
                return NULL;
            if (end_out)
                *end_out = closep + strlen(close);
            /* trim: return content with the exact span; caller bounds it
             * by the close tag position */
            return content;
        }
    }
    return NULL;
}

static char *xml_text_span(oc_arena *a, const char *content, const char *close_tag) {
    const char *closep = strstr(content, close_tag);
    if (!closep || closep == content)
        return NULL;
    /* trim whitespace both ends */
    const char *s = content;
    const char *e = closep;
    while (s < e && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n'))
        s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        e--;
    if (e <= s)
        return NULL;
    return oc_arena_strndup(a, s, (size_t)(e - s));
}

/* Value of attr="..." inside the element text starting at s (bounded by
 * the element's '>'). */
static char *xml_attr(oc_arena *a, const char *s, const char *attr) {
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

/* .csproj (SPRINT-13, docs/LANGUAGES.md C# section 6): the same bounded
 * XML reader as pom.xml -- NOT an XML parser. Comments are stripped
 * first (a commented-out PackageReference must not be read as a real
 * one). OC009 deps: <PackageReference Include="X" Version="v"/> entries
 * (attribute, or child element in the non-self-closing form) record an
 * `X` pin when the version is an EXACT literal -- alphanumeric, dot,
 * dash, plus only; `$(...)` property references, floating `*` versions
 * and ranges are NOT pins (unpinned = warn, never guessed).
 * ProjectReference entries are in-repo project links, not external
 * dependencies -- not read (recorded in the contract).
 * OC010 licence, honestly layered by what the format can declare:
 * <PackageLicenseExpression> (the SPDX expression the project
 * declares) first; else <PackageLicenseFile> naming a licence file
 * that exists among the DISCOVERED files directly inside the project
 * root; else the Go-convention project-root licence-FILE detection
 * (LICENSE, LICENCE, COPYING, NOTICE; case-insensitive basename).
 * Licence text is never parsed; none of the three -> m->license stays
 * NULL and OC010 warns, same as every other manifest. */
static int csproj_exact_pin(const char *v) {
    if (!v || v[0] == '\0')
        return 0;
    for (const char *p = v; *p; p++) {
        char c = *p;
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '.' || c == '-' || c == '+';
        if (!ok)
            return 0; /* $(), *, [, ], spaces: a moving target is not a pin */
    }
    return 1;
}

/* The project-root licence-FILE probe (the Go convention): a basename
 * exactly LICENSE, LICENCE, COPYING or NOTICE (case-insensitive)
 * directly inside the .csproj's directory, among the DISCOVERED files.
 * In-universe: reads the already-discovered file list, never the FS. */
static const char *csproj_root_licence_file(const char *path, const char *const *files,
                                            size_t n_files, oc_arena *a) {
    static const char *const LICENCE_BASES[] = {"license", "licence", "copying", "notice"};
    char root[1024];
    oc_path_dirname(path, root, sizeof(root));
    size_t rl = strlen(root);
    for (size_t i = 0; i < n_files; i++) {
        if (strncmp(files[i], root, rl) != 0 || files[i][rl] != '/')
            continue;
        const char *rest = files[i] + rl + 1;
        if (strchr(rest, '/') != NULL)
            continue; /* not directly inside the project root */
        char base[128];
        size_t bl = strlen(rest);
        if (bl == 0 || bl >= sizeof(base))
            continue;
        for (size_t c = 0; c <= bl; c++)
            base[c] = (char)((rest[c] >= 'A' && rest[c] <= 'Z') ? rest[c] - 'A' + 'a' : rest[c]);
        for (size_t k = 0; k < sizeof(LICENCE_BASES) / sizeof(LICENCE_BASES[0]); k++) {
            if (strcmp(base, LICENCE_BASES[k]) != 0)
                continue;
            return oc_arena_sprintf(a, "project-root file %s", rest);
        }
    }
    return NULL;
}

static oc_code load_csproj(oc_manifest *m, const char *path, const char *data,
                           const char *const *files, size_t n_files, oc_arena *a, oc_status *st) {
    m->kind = OC_MANIFEST_CSPROJ;
    m->eco = OC_ECO_NUGET;
    (void)path;
    char *buf = xml_strip_comments(a, data);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing .csproj buffer");
        return OC_ERR_ALLOC;
    }
    /* licence layering, step 1: PackageLicenseExpression */
    const char *expr_end = NULL;
    const char *expr = xml_block(buf, "PackageLicenseExpression", &expr_end);
    if (expr && expr_end) {
        char *t = xml_text_span(a, expr, "</PackageLicenseExpression>");
        if (t && t[0])
            m->license = t;
    }
    /* step 2: PackageLicenseFile naming a licence file that exists
     * among the discovered files in the project root */
    if (!m->license) {
        const char *lf_end = NULL;
        const char *lf = xml_block(buf, "PackageLicenseFile", &lf_end);
        if (lf && lf_end) {
            char *t = xml_text_span(a, lf, "</PackageLicenseFile>");
            if (t && t[0]) {
                char root[1024];
                oc_path_dirname(path, root, sizeof(root));
                char want[1200];
                if (snprintf(want, sizeof(want), "%s/%s", root, t) < (int)sizeof(want)) {
                    for (size_t i = 0; i < n_files; i++) {
                        if (strcmp(files[i], want) == 0) {
                            m->license = oc_arena_sprintf(a, "PackageLicenseFile %s", t);
                            break;
                        }
                    }
                }
            }
        }
    }
    /* step 3: the project-root licence-FILE convention (Go's) */
    if (!m->license)
        m->license = csproj_root_licence_file(path, files, n_files, a);
    /* dependencies: every <PackageReference> block in the WHOLE file
     * (the bounded reader does not track XML scope -- recorded here,
     * the conservative direction); attribute or child-element version */
    const char *p = buf;
    while ((p = strstr(p, "<PackageReference")) != NULL) {
        const char *gt = strchr(p, '>');
        if (!gt)
            break;
        int self_closing = gt > p && gt[-1] == '/';
        const char *block_end;
        if (self_closing) {
            block_end = gt + 1;
        } else {
            const char *closep = strstr(gt, "</PackageReference>");
            if (!closep)
                break; /* unterminated: the rest is unread, honest */
            block_end = closep + strlen("</PackageReference>");
        }
        char *inc = xml_attr(a, p, "Include");
        char *ver = xml_attr(a, p, "Version");
        if (!ver && !self_closing) {
            const char *v_end = NULL;
            const char *v = xml_block(gt, "Version", &v_end);
            if (v && v_end && v < v_end)
                ver = xml_text_span(a, v, "</Version>");
        }
        if (inc && inc[0]) {
            int pinned = csproj_exact_pin(ver);
            if (!dep_push(m, a, inc, pinned ? ver : NULL, 1, pinned && ver != NULL)) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                return OC_ERR_ALLOC;
            }
        }
        p = block_end;
    }
    return OC_OK;
}

/* ---------------------------------------- gradle (SPRINT-14) ----------- */

/* build.gradle / build.gradle.kts (docs/LANGUAGES.md Java section,
 * SPRINT-14): a BOUNDED reader for dependency coordinates -- NOT a
 * Groovy/Kotlin parser. A dependency is recorded only when the reader
 * finds a dependency-configuration keyword (outside any string/comment)
 * and, within a bounded 512-byte window after it, either an exact
 * `group:artifact:version` literal string (one optional `( )` wrapper;
 * `platform(` between keyword and literal skips the literal -- a BOM
 * node is not a versioned dependency) or the complete map form
 * (group/name/version labels in order, all three quoted). Dynamic/
 * non-exact triplet-shaped versions record as UNPINNED (warn per the
 * standing rule); anything else records nothing -- never guessed.
 * OC010 skips this manifest (gradle carries no licence field). */

static const char *const GRADLE_CONFIGS[] = {
    "api",
    "implementation",
    "compileOnly",
    "runtimeOnly",
    "testImplementation",
    "testCompileOnly",
    "testRuntimeOnly",
    "annotationProcessor",
    "compile",
    "runtime",
    NULL,
};

/* Maven standing rule (SPRINT-12 wording, applied to the gradle tier):
 * an exact literal version only -- LATEST/RELEASE and -SNAPSHOT are NOT
 * pins (the csproj charset, narrowed by the Maven reserved words). */
static int gradle_exact_pin(const char *v) {
    if (!v || !v[0])
        return 0;
    if (strcmp(v, "LATEST") == 0 || strcmp(v, "RELEASE") == 0)
        return 0;
    size_t l = strlen(v);
    if (l >= 9 && strcmp(v + l - 9, "-SNAPSHOT") == 0)
        return 0;
    for (size_t i = 0; v[i]; i++) {
        char c = v[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

/* group:artifact:version -- exactly two ':' separators, every segment
 * non-empty; characters from the honest coordinate set. Dynamic shapes
 * ("1.+", "[1.0,)", "${v}") fail and fall to the unpinned path when the
 * plain two-colon shape still holds. */
static int gradle_exact_coordinate(const char *s) {
    if (!s || !s[0])
        return 0;
    int colons = 0;
    size_t seg_start = 0;
    for (size_t i = 0; s[i]; i++) {
        char c = s[i];
        if (c == ':') {
            if (i == seg_start)
                return 0; /* empty segment */
            colons++;
            if (colons > 2)
                return 0;
            seg_start = i + 1;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-')
            continue;
        return 0;
    }
    if (strlen(s) == seg_start)
        return 0; /* empty last segment */
    return colons == 2;
}

/* any two-colon shape with non-empty segments (the unpinned fallback's
 * precondition) */
static int gradle_triplet_shape(const char *s) {
    if (!s || !s[0])
        return 0;
    int colons = 0;
    size_t seg_start = 0;
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == ':') {
            if (i == seg_start)
                return 0;
            colons++;
            if (colons > 2)
                return 0;
            seg_start = i + 1;
        }
    }
    return colons == 2 && strlen(s) > seg_start;
}

/* one label (`group`/`name`/`version`) followed by ':' and a quoted
 * string inside the window; returns the cursor after the closing quote,
 * or NULL. The label must end at a non-identifier boundary. */
static const char *gradle_labeled_value(const char *from, const char *to, const char *label,
                                        char **out, oc_arena *a) {
    size_t ll = strlen(label);
    for (const char *p = from; p + ll <= to; p++) {
        if (memcmp(p, label, ll) != 0)
            continue;
        char prev = p > from ? p[-1] : ' ';
        if ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') || prev == '_')
            continue;
        const char *q = p + ll;
        while (q < to && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r'))
            q++;
        if (q >= to || *q != ':')
            continue;
        q++;
        while (q < to && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r'))
            q++;
        if (q >= to || (*q != '\'' && *q != '"'))
            continue;
        char qc = *q++;
        const char *e = memchr(q, qc, (size_t)(to - q));
        if (!e)
            return NULL;
        size_t sl = (size_t)(e - q);
        char *s = (char *)oc_arena_alloc(a, sl + 1);
        if (!s)
            return NULL;
        memcpy(s, q, sl);
        s[sl] = '\0';
        *out = s;
        return e + 1;
    }
    return NULL;
}

static oc_code load_gradle(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                           oc_status *st) {
    m->kind = OC_MANIFEST_GRADLE;
    m->eco = OC_ECO_MAVEN;
    (void)path;
    size_t len = strlen(data);
    int in_str = 0;
    char quote = 0;
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (in_str) {
            if (c == '\\' && i + 1 < len) {
                i++;
                continue;
            }
            if (c == quote)
                in_str = 0;
            continue;
        }
        if (c == '\'' || c == '"') {
            in_str = 1;
            quote = c;
            continue;
        }
        if (c == '/' && i + 1 < len && data[i + 1] == '/') {
            while (i < len && data[i] != '\n')
                i++;
            continue;
        }
        if (c == '/' && i + 1 < len && data[i + 1] == '*') {
            const char *e = strstr(data + i + 2, "*/");
            if (!e)
                break;
            i = (size_t)(e - data) + 1;
            continue;
        }
        for (size_t k = 0; GRADLE_CONFIGS[k]; k++) {
            size_t kl = strlen(GRADLE_CONFIGS[k]);
            if (i + kl > len || memcmp(data + i, GRADLE_CONFIGS[k], kl) != 0)
                continue;
            char prev = i > 0 ? data[i - 1] : ' ';
            if ((prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') ||
                (prev >= '0' && prev <= '9') || prev == '_' || prev == '.')
                continue;
            char next = i + kl < len ? data[i + kl] : ' ';
            if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
                (next >= '0' && next <= '9') || next == '_' || next == '.')
                continue;
            /* longer configurations sort FIRST in GRADLE_CONFIGS, so
             * `compileOnly`/`runtimeOnly` win over the deprecated
             * `compile`/`runtime` shorthands at the same position */
            /* the bounded window: up to 512 bytes after the keyword, cut
             * at the first unquoted closing brace OR unquoted newline --
             * the statement-line cut. The fallback reader never reaches
             * into a later statement, so an unmatched keyword can never
             * steal a dependency from a following line. */
            const char *w = data + i + kl;
            const char *wmax = data + (i + kl + 512 < len ? i + kl + 512 : len);
            const char *wend = w;
            int win_str = 0;
            char win_quote = 0;
            while (wend < wmax) {
                char wc = *wend;
                if (win_str) {
                    if (wc == '\\' && wend + 1 < wmax) {
                        wend++;
                    } else if (wc == win_quote) {
                        win_str = 0;
                    }
                } else if (wc == '\'' || wc == '"') {
                    win_str = 1;
                    win_quote = wc;
                } else if (wc == '}' || wc == '\n') {
                    break;
                }
                wend++;
            }
            /* the first quoted string in the window */
            const char *q1 = NULL, *qe1 = NULL;
            char qc1 = 0;
            for (const char *s = w; s < wend; s++) {
                if (*s == '\'' || *s == '"') {
                    q1 = s + 1;
                    qc1 = *s;
                    break;
                }
            }
            int matched = 0;
            if (q1) {
                qe1 = memchr(q1, qc1, (size_t)(wend - q1));
                if (qe1) {
                    /* platform(...) between keyword and literal: a BOM
                     * node is not a versioned dependency -- skipped
                     * honestly */
                    int is_platform = 0;
                    for (const char *s = w; s + 9 <= q1; s++) {
                        if (memcmp(s, "platform(", 9) == 0) {
                            is_platform = 1;
                            break;
                        }
                    }
                    size_t sl = (size_t)(qe1 - q1);
                    char *lit = (char *)oc_arena_alloc(a, sl + 1);
                    if (!lit) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing gradle literal");
                        return OC_ERR_ALLOC;
                    }
                    memcpy(lit, q1, sl);
                    lit[sl] = '\0';
                    if (!is_platform && gradle_triplet_shape(lit)) {
                        char *c1 = strchr(lit, ':');
                        char *c2 = strchr(c1 + 1, ':');
                        /* the exact-pin check runs on the FULL triplet
                         * BEFORE the name/version cut */
                        int pinned = gradle_exact_coordinate(lit) && gradle_exact_pin(c2 + 1);
                        *c2 = '\0'; /* name = group:artifact */
                        if (!dep_push(m, a, lit, pinned ? c2 + 1 : NULL, 1, pinned)) {
                            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                            return OC_ERR_ALLOC;
                        }
                        matched = 1;
                        i = (size_t)(qe1 - data); /* resume after the literal */
                    }
                }
            }
            if (!matched) {
                /* the map form: group/name/version labels, in order */
                char *gv = NULL, *nv = NULL, *vv = NULL;
                const char *after = gradle_labeled_value(w, wend, "group", &gv, a);
                if (after)
                    after = gradle_labeled_value(after, wend, "name", &nv, a);
                if (after)
                    after = gradle_labeled_value(after, wend, "version", &vv, a);
                if (gv && nv && vv && gv[0] && nv[0]) {
                    char *nm = oc_arena_sprintf(a, "%s:%s", gv, nv);
                    if (!nm) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                        return OC_ERR_ALLOC;
                    }
                    int pinned = gradle_exact_pin(vv);
                    if (!dep_push(m, a, nm, pinned ? vv : NULL, 1, pinned)) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                        return OC_ERR_ALLOC;
                    }
                    matched = 1;
                    if (after)
                        i = (size_t)(after - data) - 1; /* resume after the map form */
                }
            }
            if (matched && i < (size_t)(w - data))
                i = (size_t)(w - data) - 1; /* never re-scan this keyword */
        }
    }
    return OC_OK;
}

/* -------------------------------- legacy NuGet manifests (SPRINT-14) --- */

/* packages.config: the legacy NuGet manifest. A
 * `<package id="X" version="1.2.3" ... />` element records an `X` pin
 * when the version is an exact literal (the csproj charset rule);
 * anything else records the dependency UNPINNED (warn per the standing
 * rule). OC010 skips this manifest (no licence field). */
static oc_code load_pkgs_config(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                                oc_status *st) {
    m->kind = OC_MANIFEST_PKGS_CONFIG;
    m->eco = OC_ECO_NUGET;
    (void)path;
    char *buf = xml_strip_comments(a, data);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing packages.config buffer");
        return OC_ERR_ALLOC;
    }
    const char *p = buf;
    while ((p = strstr(p, "<package")) != NULL) {
        /* the <packages> wrapper is not a dependency tag (the next char
         * must not be an identifier character) */
        char na = p[8];
        if ((na >= 'a' && na <= 'z') || (na >= 'A' && na <= 'Z') || (na >= '0' && na <= '9') ||
            na == '_' || na == ':') {
            p += 8;
            continue;
        }
        const char *gt = strchr(p, '>');
        if (!gt)
            break; /* unterminated tag: the rest is unread, honest */
        /* attribute lookup is bounded to THIS tag's text: an id-less tag
         * must never steal a later tag's id="..." (xml_attr searches
         * forward from its argument) */
        char tag[4096];
        size_t tl = (size_t)(gt - p);
        if (tl >= sizeof(tag)) {
            p = gt + 1;
            continue; /* pathological tag: unread, honest */
        }
        memcpy(tag, p, tl);
        tag[tl] = '\0';
        char *id = xml_attr(a, tag, "id");
        char *ver = xml_attr(a, tag, "version");
        if (id && id[0]) {
            int pinned = ver && csproj_exact_pin(ver);
            if (!dep_push(m, a, id, pinned ? ver : NULL, 1, pinned))
                return OC_ERR_ALLOC;
        }
        p = gt + 1;
    }
    return OC_OK;
}

/* packages.lock.json: the NuGet lockfile. A `dependencies.<name>` entry
 * whose `resolved` field is an exact literal records the RESOLVED pin
 * (the `requested` range is NEVER substituted -- never guessed); both
 * `type: "Direct"` and `type: "Transitive"` entries are read (transitive
 * pins are real build inputs -- the go.mod `// indirect` precedent).
 * OC010 skips this manifest (no licence field). */
static oc_code load_pkgs_lock(oc_manifest *m, const char *path, const char *data, size_t len,
                              oc_arena *a, oc_status *st) {
    m->kind = OC_MANIFEST_PKGS_LOCK;
    m->eco = OC_ECO_NUGET;
    (void)path;
    oc_json *root = oc_json_parse(a, data, len, st);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "manifest '%s' is not a JSON object", path);
        return OC_ERR_USAGE;
    }
    const oc_json *deps = oc_json_get(root, "dependencies");
    if (!deps || deps->type != OC_JSON_OBJ)
        return OC_OK; /* nothing recorded (honest) */
    for (size_t i = 0; i < deps->n_fields; i++) {
        const char *name = deps->keys[i];
        const oc_json *e = deps->vals[i];
        if (!name || !name[0] || !e || e->type != OC_JSON_OBJ)
            continue;
        const char *resolved = oc_json_as_str(oc_json_get(e, "resolved"), "");
        int pinned = resolved[0] && csproj_exact_pin(resolved);
        if (!dep_push(m, a, name, pinned ? resolved : NULL, 1, pinned))
            return OC_ERR_ALLOC;
    }
    return OC_OK;
}

/* ------------------------------------------------- Cargo (SPRINT-15) --- */

/* Cargo requirement strings are honestly matchable only in the EXACT
 * form: `=x.y.z...` (the leading `=` is Cargo's own exact operator; the
 * rest is a clean version -- alnum/dot/dash/plus). Caret/tilde/star/
 * comparison/range forms can name many versions: NOT pins (unpinned =
 * warn, never guessed). `*out` receives the version without the `=`. */
static int cargo_exact_pin(const char *req, const char **out) {
    if (!req || req[0] != '=')
        return 0;
    const char *v = req + 1;
    if (!v[0])
        return 0;
    for (const char *q = v; *q; q++) {
        char c = *q;
        int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 c == '.' || c == '-' || c == '+';
        if (!ok)
            return 0;
    }
    *out = v;
    return 1;
}

/* Strips a `#` comment from one line, respecting double-quoted strings
 * (a # inside a requirement value is data, not a comment). */
static void cargo_strip_comment(char *s, size_t *len) {
    int in_str = 0;
    for (size_t i = 0; i < *len; i++) {
        if (s[i] == '"')
            in_str = !in_str;
        else if (s[i] == '#' && !in_str) {
            *len = i;
            s[i] = '\0';
            return;
        }
    }
}

/* Copies one double-quoted string's payload out of `from`. Returns an
 * arena copy or NULL when the line carries no quoted string. */
static char *cargo_quoted(oc_arena *a, const char *from) {
    const char *q1 = strchr(from, '"');
    if (!q1)
        return NULL;
    const char *q2 = strchr(q1 + 1, '"');
    if (!q2 || q2 == q1 + 1)
        return NULL;
    return oc_arena_strndup(a, q1 + 1, (size_t)(q2 - q1 - 1));
}

/* TRUE when the inline table `{ ... }` on this line declares
 * `workspace = true` (inheritance from [workspace.dependencies]). */
static int cargo_is_workspace_inherit(const char *line) {
    const char *brace = strchr(line, '{');
    if (!brace)
        return 0;
    const char *w = strstr(brace, "workspace");
    if (!w)
        return 0;
    w += strlen("workspace");
    while (*w == ' ' || *w == '\t')
        w++;
    return w[0] == '=' && w[1] != '\0' && w[1] != '"';
}

/* TRUE when the inline table declares `path = ...` (an in-repo link:
 * not an external dependency, not OC009 material -- the ProjectReference
 * precedent). */
static int cargo_is_path_dep(const char *line) {
    const char *brace = strchr(line, '{');
    if (!brace)
        return 0;
    const char *w = strstr(brace, "path");
    if (!w)
        return 0;
    w += strlen("path");
    while (*w == ' ' || *w == '\t')
        w++;
    return w[0] == '=';
}

/* Is `table` (the current [section] header text) one of the dependency
 * tables the reader consumes? [dependencies], [dev-dependencies],
 * [build-dependencies], [workspace.dependencies], and the
 * [target.'cfg(...)'.dependencies] forms (dev/build variants included).
 * docs/LANGUAGES.md Rust section 6. */
static int cargo_is_dep_table(const char *table) {
    static const char *const SUFFIXES[] = {"dependencies", "dev-dependencies",
                                           "build-dependencies"};
    static const char *const EXACT[] = {"dependencies", "dev-dependencies", "build-dependencies",
                                        "workspace.dependencies"};
    for (size_t i = 0; i < sizeof(EXACT) / sizeof(EXACT[0]); i++)
        if (strcmp(table, EXACT[i]) == 0)
            return 1;
    /* [target.'cfg(...)'.X] / [target.<triple>.X] -- the leaf segment
     * after the last dot decides */
    const char *dot = strrchr(table, '.');
    if (dot && strncmp(table, "target.", 7) == 0) {
        for (size_t i = 0; i < sizeof(SUFFIXES) / sizeof(SUFFIXES[0]); i++)
            if (strcmp(dot + 1, SUFFIXES[i]) == 0)
                return 1;
    }
    return 0;
}

/* Pass 1 over the buffer: collect [workspace.dependencies] entries
 * (name -> version + pinned) for the workspace-inheritance resolution.
 * At most 64 entries (bounded, pack-data scale). */
typedef struct {
    char *name[64];
    char *version[64];
    int pinned[64];
    size_t n;
} cargo_ws_table;

static void cargo_scan_workspace(oc_arena *a, const char *data, cargo_ws_table *ws) {
    memset(ws, 0, sizeof(*ws));
    char linebuf[1024];
    const char *p = data;
    int in_ws = 0;
    while (p && *p && ws->n < 64) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > 0 && p[ll - 1] == '\r')
            ll--;
        if (ll < sizeof(linebuf)) {
            memcpy(linebuf, p, ll);
            linebuf[ll] = '\0';
            size_t bl = ll;
            cargo_strip_comment(linebuf, &bl);
            linebuf[bl] = '\0';
            const char *s = linebuf;
            while (*s == ' ' || *s == '\t')
                s++;
            if (s[0] == '[') {
                in_ws = strncmp(s, "[workspace.dependencies]", 24) == 0;
            } else if (in_ws && s[0] != '\0') {
                const char *eq = strchr(s, '=');
                if (eq) {
                    char *req = cargo_quoted(a, eq);
                    if (req) {
                        const char *ver;
                        ws->name[ws->n] = oc_arena_strndup(a, s, (size_t)(eq - s));
                        /* trim the key's trailing spaces */
                        char *k = ws->name[ws->n];
                        size_t kl = strlen(k);
                        while (kl > 0 && (k[kl - 1] == ' ' || k[kl - 1] == '\t'))
                            k[--kl] = '\0';
                        ws->pinned[ws->n] = cargo_exact_pin(req, &ver);
                        ws->version[ws->n] =
                            ws->pinned[ws->n] ? oc_arena_strdup(a, ver) : oc_arena_strdup(a, req);
                        ws->n++;
                    }
                }
            }
        }
        p = eol ? eol + 1 : NULL;
    }
}

static const char *cargo_ws_lookup(cargo_ws_table *ws, const char *name, size_t nl, int *pinned) {
    for (size_t i = 0; i < ws->n; i++) {
        if (strlen(ws->name[i]) == nl && memcmp(ws->name[i], name, nl) == 0) {
            *pinned = ws->pinned[i];
            return ws->version[i];
        }
    }
    return NULL;
}

/* Cargo.toml (SPRINT-15, docs/LANGUAGES.md Rust section 6). Line-scanned
 * bounded reader (the requirements.txt/pyproject discipline): the
 * dependency tables [dependencies] / [dev-dependencies] /
 * [build-dependencies] / [target.'cfg(...)'.dependencies] and
 * [workspace.dependencies] are all read (dev/build/transitive pins are
 * real build inputs). A requirement is a PIN iff it is the exact form
 * `=x.y.z`; everything else records unpinned (OC009 warns -- Cargo's own
 * semantics, never guessed). `{ path = ... }` entries are in-repo links:
 * nothing recorded. `{ workspace = true }` entries inherit the same
 * manifest's [workspace.dependencies] entry by name (version and
 * pin-ness verbatim); an entry the workspace table does not name records
 * NOTHING (the version is unknowable from this file alone). `{ git = ...
 * }` without a version is an unpinned dependency. OC010 licence,
 * honestly layered: [package] license = "..." first; else license-file =
 * "..." naming a licence file that exists among the DISCOVERED files
 * directly inside the project root; else the Go-convention project-root
 * licence-FILE detection (LICENSE, LICENCE, COPYING, NOTICE;
 * case-insensitive basename); none -> NULL (the warn). */
static oc_code load_cargo_toml(oc_manifest *m, const char *path, const char *data,
                               const char *const *files, size_t n_files, oc_arena *a,
                               oc_status *st) {
    m->kind = OC_MANIFEST_CARGO_TOML;
    m->eco = OC_ECO_CRATES;
    cargo_ws_table ws;
    cargo_scan_workspace(a, data, &ws);
    char *buf = oc_arena_alloc(a, strlen(data) + 1);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing Cargo.toml line buffer");
        return OC_ERR_ALLOC;
    }
    size_t lineno = 0;
    char table[256] = "";
    int in_package = 0;
    char *license = NULL;
    char *license_file = NULL;
    const char *p = data;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > 0 && p[ll - 1] == '\r')
            ll--;
        lineno++;
        if (ll >= strlen(data) + 1) {
            p = eol ? eol + 1 : NULL;
            continue;
        }
        memcpy(buf, p, ll);
        buf[ll] = '\0';
        size_t bl = ll;
        cargo_strip_comment(buf, &bl);
        buf[bl] = '\0';
        char *s = buf;
        while (*s == ' ' || *s == '\t')
            s++;
        if (s[0] == '[') {
            const char *close = strrchr(s, ']');
            if (close && (size_t)(close - s) < sizeof(table)) {
                char cand[256];
                size_t cl = (size_t)(close - s - 1);
                memcpy(cand, s + 1, cl);
                cand[cl] = '\0';
                snprintf(table, sizeof(table), "%s", cand);
                in_package = strcmp(table, "package") == 0;
            }
        } else if (s[0] != '\0' && s[0] != '#') {
            const char *eq = strchr(s, '=');
            if (eq) {
                size_t kl = (size_t)(eq - s);
                while (kl > 0 && (s[kl - 1] == ' ' || s[kl - 1] == '\t'))
                    kl--;
                if (in_package) {
                    if ((kl == 7 && memcmp(s, "license", 7) == 0) && !license) {
                        license = cargo_quoted(a, eq);
                    } else if (kl == 12 && memcmp(s, "license-file", 12) == 0 && !license_file) {
                        license_file = cargo_quoted(a, eq);
                    }
                } else if (cargo_is_dep_table(table)) {
                    char name[256];
                    if (kl == 0 || kl >= sizeof(name))
                        goto next_line;
                    memcpy(name, s, kl);
                    name[kl] = '\0';
                    if (cargo_is_workspace_inherit(eq)) {
                        int wpinned = 0;
                        const char *wver = cargo_ws_lookup(&ws, name, kl, &wpinned);
                        if (wver) {
                            if (!dep_push(m, a, name, wpinned ? wver : NULL, lineno, wpinned)) {
                                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                                return OC_ERR_ALLOC;
                            }
                        }
                        /* the workspace table does not name it: records
                         * NOTHING (never guessed) */
                        goto next_line;
                    }
                    if (cargo_is_path_dep(eq))
                        goto next_line; /* in-repo link: not OC009 material */
                    char *req = cargo_quoted(a, eq);
                    if (!req) {
                        /* inline table without a plain requirement: look
                         * for a version = "..." field inside the braces */
                        const char *brace = strchr(eq, '{');
                        const char *close = brace ? strchr(brace, '}') : NULL;
                        if (brace && close) {
                            const char *vw = strstr(brace, "version");
                            if (vw && vw < close) {
                                req = cargo_quoted(a, vw);
                            } else {
                                const char *gw = strstr(brace, "git");
                                if (gw && gw < close) {
                                    /* a git dependency without a version:
                                     * unpinned (warn) */
                                    if (!dep_push(m, a, name, NULL, lineno, 0)) {
                                        oc_status_set(st, OC_ERR_ALLOC,
                                                      "out of memory storing dependency");
                                        return OC_ERR_ALLOC;
                                    }
                                    goto next_line;
                                }
                            }
                        }
                    }
                    if (req) {
                        const char *ver;
                        int pinned = cargo_exact_pin(req, &ver);
                        if (!dep_push(m, a, name, pinned ? ver : NULL, lineno, pinned)) {
                            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                            return OC_ERR_ALLOC;
                        }
                    }
                    /* no version, no git: an unparseable entry records
                     * nothing (never guessed) */
                }
            }
        }
    next_line:
        p = eol ? eol + 1 : NULL;
    }
    /* OC010 licence layering, steps 1-3 */
    if (license && license[0]) {
        m->license = license;
        return OC_OK;
    }
    if (license_file && license_file[0]) {
        /* step 2: the named file must exist among the DISCOVERED files
         * directly inside the project root */
        char root[1024];
        oc_path_dirname(path, root, sizeof(root));
        size_t rl = strlen(root);
        for (size_t i = 0; i < n_files; i++) {
            if (strncmp(files[i], root, rl) != 0 || files[i][rl] != '/')
                continue;
            const char *rest = files[i] + rl + 1;
            if (strchr(rest, '/') != NULL)
                continue;
            if (strcmp(rest, license_file) == 0) {
                m->license = oc_arena_sprintf(a, "license-file %s", license_file);
                return OC_OK;
            }
        }
    }
    /* step 3: the project-root licence-FILE convention (Go's) */
    m->license = csproj_root_licence_file(path, files, n_files, a);
    return OC_OK;
}

/* Cargo.lock (SPRINT-15): the lockfile. Every [[package]] entry that
 * carries a `source` field records the RESOLVED pin (registry and git
 * sources are external build inputs -- the packages.lock.json resolved-
 * pin discipline); entries WITHOUT a `source` are the workspace members
 * themselves and record nothing. The requirement ranges of Cargo.toml
 * are never substituted (never guessed). OC010 skips this manifest (no
 * licence field). Line-scanned: entries carry real line numbers. */
static oc_code load_cargo_lock(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                               oc_status *st) {
    m->kind = OC_MANIFEST_CARGO_LOCK;
    m->eco = OC_ECO_CRATES;
    (void)path;
    char *buf = oc_arena_alloc(a, strlen(data) + 1);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing Cargo.lock line buffer");
        return OC_ERR_ALLOC;
    }
    size_t lineno = 0;
    int in_pkg = 0;
    char *name = NULL;
    char *version = NULL;
    int has_source = 0;
    size_t name_line = 0;
    oc_code rc = OC_OK;
    const char *p = data;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > 0 && p[ll - 1] == '\r')
            ll--;
        lineno++;
        if (ll >= strlen(data) + 1) {
            p = eol ? eol + 1 : NULL;
            continue;
        }
        memcpy(buf, p, ll);
        buf[ll] = '\0';
        size_t bl = ll;
        cargo_strip_comment(buf, &bl);
        buf[bl] = '\0';
        char *s = buf;
        while (*s == ' ' || *s == '\t')
            s++;
        if (s[0] == '[') {
            /* flush the previous package on block change */
            if (in_pkg && name && version && has_source && rc == OC_OK) {
                if (!dep_push(m, a, name, version, name_line, 1))
                    rc = OC_ERR_ALLOC;
            }
            name = NULL;
            version = NULL;
            has_source = 0;
            name_line = 0;
            in_pkg = strncmp(s, "[[package]]", 11) == 0;
        } else if (in_pkg && s[0] != '\0') {
            const char *eq = strchr(s, '=');
            if (eq) {
                size_t kl = (size_t)(eq - s);
                while (kl > 0 && (s[kl - 1] == ' ' || s[kl - 1] == '\t'))
                    kl--;
                if (kl == 4 && memcmp(s, "name", 4) == 0) {
                    name = cargo_quoted(a, eq);
                    name_line = lineno;
                } else if (kl == 7 && memcmp(s, "version", 7) == 0) {
                    version = cargo_quoted(a, eq);
                } else if (kl == 6 && memcmp(s, "source", 6) == 0) {
                    has_source = cargo_quoted(a, eq) != NULL;
                }
            }
        }
        p = eol ? eol + 1 : NULL;
    }
    if (rc == OC_OK && in_pkg && name && version && has_source) {
        if (!dep_push(m, a, name, version, name_line, 1))
            rc = OC_ERR_ALLOC;
    }
    if (rc == OC_ERR_ALLOC) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
        return OC_ERR_ALLOC;
    }
    return OC_OK;
}

/* Gemfile.lock (SPRINT-16, docs/LANGUAGES.md Ruby section 6): the GEM
 * section's specs entries -- `name (version)` at exactly 4-space indent
 * -- are the RESOLVED pins (the Cargo.lock resolved-pin discipline);
 * the 6-space sub-entries under a gem are that gem's own REQUESTED
 * ranges and record NOTHING (every transitively loaded gem's resolved
 * pin appears at the 4-space level anyway); GIT and PATH sections are
 * remote-source/in-repo links, skipped entirely (the Cargo
 * `{ path = ... }` precedent); DEPENDENCIES, PLATFORMS and BUNDLED WITH
 * are not pin material. Bounded line reader, the go.mod discipline. */
static oc_code load_gemfile_lock(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                                 oc_status *st) {
    m->kind = OC_MANIFEST_GEMFILE_LOCK;
    m->eco = OC_ECO_RUBYGEMS;
    (void)path;
    char *buf = oc_arena_alloc(a, strlen(data) + 1);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing Gemfile.lock line buffer");
        return OC_ERR_ALLOC;
    }
    int in_gem_section = 0;
    int in_specs = 0;
    size_t lineno = 0;
    const char *p = data;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > 0 && p[ll - 1] == '\r')
            ll--;
        lineno++;
        memcpy(buf, p, ll);
        buf[ll] = '\0';
        char *s = buf;
        size_t indent = 0;
        while (*s == ' ' || *s == '\t') {
            indent += (*s == '\t') ? 2 : 1;
            s++;
        }
        if (*s != '\0') {
            if (indent == 0) {
                /* a top-level section header decides what is read */
                in_gem_section = strcmp(s, "GEM") == 0;
                in_specs = 0;
            } else if (in_gem_section) {
                if (indent == 2) {
                    in_specs = strcmp(s, "specs:") == 0;
                } else if (in_specs && indent == 4) {
                    /* the resolved pin: `name (version)` */
                    const char *open = strchr(s, '(');
                    const char *close = open ? strchr(open + 1, ')') : NULL;
                    if (open && close && close > open + 1 && open > s) {
                        size_t nl = (size_t)(open - s);
                        while (nl > 0 && (s[nl - 1] == ' ' || s[nl - 1] == '\t'))
                            nl--;
                        if (nl > 0) {
                            size_t vl = (size_t)(close - open - 1);
                            char *name = oc_arena_strndup(a, s, nl);
                            char *ver = oc_arena_strndup(a, open + 1, vl);
                            if (!name || !ver || !dep_push(m, a, name, ver, lineno, 1)) {
                                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                                return OC_ERR_ALLOC;
                            }
                        }
                    }
                }
                /* 6-space entries (a gem's own requested ranges) and any
                 * other indent record NOTHING (the contract) */
            }
            /* skipped sections and blank lines: nothing recorded */
        }
        p = eol ? eol + 1 : NULL;
    }
    return OC_OK;
}

/* Gemfile (SPRINT-16, docs/LANGUAGES.md Ruby section 6): a bounded
 * single-line reader for `gem "name"[, "spec"]*` calls (comments
 * stripped; the dep name is the first string argument; a `path:` or
 * `:path =>` option marks an in-repo link and records nothing -- the
 * Cargo path-dep precedent). A spec string is a PIN iff it is the bare
 * version form ("1.2.3" -- bundler's own exact-requirement semantics)
 * or the explicit "= x.y.z"; `~>`, `>=`, `>`, `<`, `<=`, `!=` and
 * ranges are NOT pins -- unpinned = warn, never guessed. No spec at all
 * is unpinned. This is NOT a Ruby parser; anything the bounded reader
 * cannot see stays unread (the .gemspec boundary). */
static int gemfile_exact_pin(const char *spec) {
    /* bare version: every char is a digit or a dot, at least one digit,
     * no comparator operator anywhere */
    if (spec[0] == '\0')
        return 0;
    if (spec[0] == '=' && spec[1] == ' ')
        return 1; /* the explicit "= x.y.z" exact form */
    for (const char *q = spec; *q; q++) {
        if (*q == '~' || *q == '>' || *q == '<' || *q == '!')
            return 0; /* a comparator range: bundler semantics, not a pin */
        if (*q != '.' && (*q < '0' || *q > '9'))
            return 0;
    }
    return 1;
}

static oc_code load_gemfile(oc_manifest *m, const char *path, const char *data, oc_arena *a,
                            oc_status *st) {
    m->kind = OC_MANIFEST_GEMFILE;
    m->eco = OC_ECO_RUBYGEMS;
    (void)path;
    char *buf = oc_arena_alloc(a, strlen(data) + 1);
    if (!buf) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing Gemfile line buffer");
        return OC_ERR_ALLOC;
    }
    size_t lineno = 0;
    const char *p = data;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        if (ll > 0 && p[ll - 1] == '\r')
            ll--;
        lineno++;
        memcpy(buf, p, ll);
        buf[ll] = '\0';
        /* strip a `#` comment (Ruby line comment; a bounded reader never
         * sees strings spanning lines) -- first `#` outside the quoted
         * name/spec scan below would misfire, so scan quotes first: the
         * reader strips comments only at a `#` that is not inside a
         * quoted region. */
        int in_q = 0;
        char qch = 0;
        for (size_t i = 0; buf[i]; i++) {
            if (!in_q && (buf[i] == '\'' || buf[i] == '"')) {
                in_q = 1;
                qch = buf[i];
            } else if (in_q && buf[i] == qch) {
                in_q = 0;
            } else if (!in_q && buf[i] == '#') {
                buf[i] = '\0';
                break;
            }
        }
        /* find the gem call: an identifier `gem` followed by a quoted
         * name (the first string literal on the line) */
        char *s = buf;
        while (*s == ' ' || *s == '\t')
            s++;
        size_t gl = strlen("gem");
        int is_gem_call =
            strncmp(s, "gem", gl) == 0 && (s[gl] == ' ' || s[gl] == '\t' || s[gl] == '(');
        if (is_gem_call) {
            const char *q1 = strchr(s + gl, '"');
            const char *q1s = strchr(s + gl, '\'');
            if (q1s && (!q1 || q1s < q1))
                q1 = q1s;
            if (q1) {
                char qch2 = *q1;
                const char *q2 = strchr(q1 + 1, qch2);
                if (q2 && q2 > q1 + 1) {
                    size_t nl = (size_t)(q2 - q1 - 1);
                    char *name = oc_arena_strndup(a, q1 + 1, nl);
                    if (!name) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                        return OC_ERR_ALLOC;
                    }
                    /* in-repo link: a path: option anywhere on the line
                     * (the Cargo { path = ... } precedent: not OC009
                     * material, the code lives in this repo) */
                    if (strstr(s, "path:") != NULL || strstr(s, ":path") != NULL) {
                        p = eol ? eol + 1 : NULL;
                        continue;
                    }
                    /* the FIRST remaining quoted string is the spec */
                    const char *rest = q2 + 1;
                    const char *sp1 = strchr(rest, '"');
                    const char *sp1s = strchr(rest, '\'');
                    if (sp1s && (!sp1 || sp1s < sp1))
                        sp1 = sp1s;
                    const char *ver = NULL;
                    int pinned = 0;
                    if (sp1) {
                        char qch3 = *sp1;
                        const char *sp2 = strchr(sp1 + 1, qch3);
                        if (sp2 && sp2 > sp1 + 1) {
                            size_t vl = (size_t)(sp2 - sp1 - 1);
                            char *spec = oc_arena_strndup(a, sp1 + 1, vl);
                            if (!spec) {
                                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                                return OC_ERR_ALLOC;
                            }
                            if (gemfile_exact_pin(spec)) {
                                /* the explicit "= x.y.z" form records the
                                 * bare version */
                                ver = spec[0] == '=' ? spec + 2 : spec;
                                pinned = 1;
                            }
                        }
                    }
                    if (!dep_push(m, a, name, ver, lineno, pinned)) {
                        oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                        return OC_ERR_ALLOC;
                    }
                }
            }
        }
        p = eol ? eol + 1 : NULL;
    }
    return OC_OK;
}

/* The Gemfile/Gemfile.lock licence probe: the manifests carry NO licence
 * field -- the project-root licence-FILE probe (the go.mod discipline;
 * LICENSE, LICENCE, COPYING, NOTICE; case-insensitive basename among the
 * discovered files directly inside the project root). */
static void gem_detect_licence(oc_manifest *m, oc_arena *a, const char *const *files,
                               size_t n_files) {
    gomod_detect_licence(m, a, files, n_files); /* same rule, same bases */
}

/* Platform requirements (php, ext-*, lib-*, php-*, composer, hhvm) name
 * the runtime, not a packagist package: both composer readers record
 * nothing for them (never guessed). */
static int composer_platform_name(const char *name) {
    return strcmp(name, "php") == 0 || strcmp(name, "composer") == 0 || strcmp(name, "hhvm") == 0 ||
           strncmp(name, "ext-", 4) == 0 || strncmp(name, "lib-", 4) == 0 ||
           strncmp(name, "php-", 4) == 0;
}

/* composer.json (SPRINT-19, docs/LANGUAGES.md PHP section 6): the
 * bounded reader takes `require` / `require-dev` string version specs
 * and the `license` field. A composer spec is honestly matchable only
 * when it is an EXACT pin: digits and dots (optionally a leading 'v')
 * -- constraint ranges (^ ~ > < >= <= * x || - and every other operator
 * form) can name many versions and are NOT pins, unpinned = warn, never
 * guessed (composer's own semantics; the npm discipline). The autoload
 * PSR-4/PSR-0 prefixes are RESOLVER CONFIG (not an imports edge) and
 * are read by the resolver directly from the analysis root; `path`
 * repositories are in-repo links, not OC009 material of their own. The
 * licence conventions follow package.json (a non-empty `license`
 * string; composer.lock is the OC009-only lockfile tier). */
static oc_code load_composer_json(oc_manifest *m, const char *path, const char *data, size_t len,
                                  oc_arena *a, oc_status *st) {
    m->kind = OC_MANIFEST_COMPOSER_JSON;
    m->eco = OC_ECO_PACKAGIST;
    oc_json *root = oc_json_parse(a, data, len, st);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "manifest '%s' is not a JSON object", path);
        return OC_ERR_USAGE;
    }
    const oc_json *lic = oc_json_get(root, "license");
    if (lic && lic->type == OC_JSON_STR && lic->str[0])
        m->license = lic->str;
    for (size_t s = 0; s < 2; s++) { /* require, then require-dev */
        const char *section = s == 0 ? "require" : "require-dev";
        const oc_json *deps = oc_json_get(root, section);
        if (!deps || deps->type != OC_JSON_OBJ)
            continue;
        for (size_t i = 0; i < deps->n_fields; i++) {
            const char *name = deps->keys[i];
            const char *spec = oc_json_as_str(deps->vals[i], "");
            if (composer_platform_name(name))
                continue;
            if (!npm_exact_pin(spec))
                continue; /* constraint ranges: never guessed (see above) */
            if (!dep_push(m, a, name, spec, 1, 1)) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                return OC_ERR_ALLOC;
            }
        }
    }
    return OC_OK;
}

/* composer.lock (SPRINT-19): the `packages` + `packages-dev` arrays'
 * `name` + `version` entries are the RESOLVED pins (a lock file is
 * exact by definition -- the Gemfile.lock / Cargo.lock discipline);
 * platform entries in those arrays record nothing. The lockfile tier
 * is OC009-only (no licence field). */
static oc_code load_composer_lock(oc_manifest *m, const char *path, const char *data, size_t len,
                                  oc_arena *a, oc_status *st) {
    m->kind = OC_MANIFEST_COMPOSER_LOCK;
    m->eco = OC_ECO_PACKAGIST;
    oc_json *root = oc_json_parse(a, data, len, st);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "manifest '%s' is not a JSON object", path);
        return OC_ERR_USAGE;
    }
    static const char *const SECTIONS[] = {"packages", "packages-dev"};
    for (size_t s = 0; s < 2; s++) {
        const oc_json *arr = oc_json_get(root, SECTIONS[s]);
        if (!arr || arr->type != OC_JSON_ARR)
            continue;
        for (size_t i = 0; i < arr->n_items; i++) {
            const oc_json *pkg = arr->items[i];
            if (!pkg || pkg->type != OC_JSON_OBJ)
                continue;
            const oc_json *nm = oc_json_get(pkg, "name");
            const oc_json *ver = oc_json_get(pkg, "version");
            if (!nm || nm->type != OC_JSON_STR || !nm->str[0])
                continue;
            if (!ver || ver->type != OC_JSON_STR || !ver->str[0])
                continue;
            if (composer_platform_name(nm->str))
                continue;
            if (!dep_push(m, a, nm->str, ver->str, 1, 1)) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory storing dependency");
                return OC_ERR_ALLOC;
            }
        }
    }
    return OC_OK;
}

static oc_manifest_kind manifest_kind_of(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strcmp(base, "package.json") == 0)
        return OC_MANIFEST_PACKAGE_JSON;
    if (strcmp(base, "pyproject.toml") == 0)
        return OC_MANIFEST_PYPROJECT;
    if (strcmp(base, "requirements.txt") == 0)
        return OC_MANIFEST_REQUIREMENTS;
    if (strcmp(base, "go.mod") == 0)
        return OC_MANIFEST_GOMOD;
    if (strcmp(base, "pom.xml") == 0)
        return OC_MANIFEST_POM;
    /* any SDK-style project file is a manifest: C# projects are named
     * after the assembly (Foo.csproj), unlike the single-basename
     * manifests above (SPRINT-13, docs/LANGUAGES.md C# section 6) */
    size_t bl = strlen(base);
    if (bl > 7 && strcmp(base + bl - 7, ".csproj") == 0)
        return OC_MANIFEST_CSPROJ;
    if (strcmp(base, "build.gradle") == 0 || strcmp(base, "build.gradle.kts") == 0)
        return OC_MANIFEST_GRADLE;
    if (strcmp(base, "packages.config") == 0)
        return OC_MANIFEST_PKGS_CONFIG;
    if (strcmp(base, "packages.lock.json") == 0)
        return OC_MANIFEST_PKGS_LOCK;
    if (strcmp(base, "Cargo.toml") == 0)
        return OC_MANIFEST_CARGO_TOML;
    if (strcmp(base, "Cargo.lock") == 0)
        return OC_MANIFEST_CARGO_LOCK;
    if (strcmp(base, "Gemfile") == 0)
        return OC_MANIFEST_GEMFILE;
    if (strcmp(base, "Gemfile.lock") == 0)
        return OC_MANIFEST_GEMFILE_LOCK;
    if (strcmp(base, "composer.json") == 0)
        return OC_MANIFEST_COMPOSER_JSON;
    if (strcmp(base, "composer.lock") == 0)
        return OC_MANIFEST_COMPOSER_LOCK;
    return (oc_manifest_kind)-1;
}

oc_code oc_supply_load_manifests(oc_manifest_vec *out, const char *const *files, size_t n_files,
                                 oc_arena *a, oc_status *st) {
    oc_manifests_init(out);
    for (size_t i = 0; i < n_files; i++) {
        oc_manifest_kind k = manifest_kind_of(files[i]);
        if (k == (oc_manifest_kind)-1)
            continue;
        oc_manifest m;
        memset(&m, 0, sizeof(m));
        m.path = oc_arena_strdup(a, files[i]);
        if (!m.path) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing manifest path");
            return OC_ERR_ALLOC;
        }
        char *data = NULL;
        size_t len = 0;
        oc_code rc = oc_read_file(files[i], &data, &len, st);
        if (rc != OC_OK)
            return rc;
        switch (k) {
        case OC_MANIFEST_PACKAGE_JSON:
            rc = load_package_json(&m, files[i], data, len, a, st);
            break;
        case OC_MANIFEST_PYPROJECT:
            rc = load_pyproject(&m, files[i], data, a, st);
            break;
        case OC_MANIFEST_REQUIREMENTS:
            rc = load_requirements(&m, files[i], data, a, st);
            break;
        case OC_MANIFEST_POM:
            rc = load_pom(&m, files[i], data, a, st);
            break;
        case OC_MANIFEST_GOMOD:
            rc = load_gomod(&m, files[i], data, a, st);
            if (rc == OC_OK)
                gomod_detect_licence(&m, a, files, n_files);
            break;
        case OC_MANIFEST_CSPROJ:
            rc = load_csproj(&m, files[i], data, files, n_files, a, st);
            break;
        case OC_MANIFEST_GRADLE:
            rc = load_gradle(&m, files[i], data, a, st);
            break;
        case OC_MANIFEST_PKGS_CONFIG:
            rc = load_pkgs_config(&m, files[i], data, a, st);
            break;
        case OC_MANIFEST_PKGS_LOCK:
            rc = load_pkgs_lock(&m, files[i], data, len, a, st);
            break;
        case OC_MANIFEST_CARGO_TOML:
            rc = load_cargo_toml(&m, files[i], data, files, n_files, a, st);
            break;
        case OC_MANIFEST_CARGO_LOCK:
            rc = load_cargo_lock(&m, files[i], data, a, st);
            break;
        case OC_MANIFEST_GEMFILE:
            rc = load_gemfile(&m, files[i], data, a, st);
            if (rc == OC_OK)
                gem_detect_licence(&m, a, files, n_files);
            break;
        case OC_MANIFEST_GEMFILE_LOCK:
            rc = load_gemfile_lock(&m, files[i], data, a, st);
            if (rc == OC_OK)
                gem_detect_licence(&m, a, files, n_files);
            break;
        case OC_MANIFEST_COMPOSER_JSON:
            rc = load_composer_json(&m, files[i], data, len, a, st);
            break;
        case OC_MANIFEST_COMPOSER_LOCK:
            rc = load_composer_lock(&m, files[i], data, len, a, st);
            break;
        default:
            rc = OC_OK;
            break;
        }
        free(data);
        if (rc != OC_OK)
            return rc;
        if (!manifest_push(out, m)) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory storing manifest");
            return OC_ERR_ALLOC;
        }
    }
    return OC_OK;
}

/* ----------------------------------------------------------------- OSV */

void oc_osv_init(oc_osv_snapshot *s) {
    memset(s, 0, sizeof(*s));
}

void oc_osv_destroy(oc_osv_snapshot *s) {
    for (size_t i = 0; i < s->n_records; i++) {
        for (size_t p = 0; p < s->records[i].n_packages; p++)
            free(s->records[i].packages[p].ranges);
        free(s->records[i].packages);
    }
    free(s->records);
    oc_osv_init(s);
}

int oc_version_cmp(const char *a, const char *b) {
    /* Dotted numeric compare; a non-numeric suffix ('-beta', 'b0') sorts
     * before the plain release: 1.0.0-beta < 1.0.0. A leading 'v' is a
     * SCHEME prefix, not a version segment (go.mod says "v1.2.3" while
     * OSV records and npm/PyPI say "1.2.3"; npm pins allow the same
     * leading-v form) -- skipped on both sides before comparing
     * (SPRINT-11: the Go ecosystem needs this boundary to be honest). */
    if (*a == 'v' || *a == 'V')
        a++;
    if (*b == 'v' || *b == 'V')
        b++;
    size_t ia = 0, ib = 0;
    for (;;) {
        long na = 0, nb = 0;
        int sa = 0, sb = 0;
        while (a[ia] >= '0' && a[ia] <= '9') {
            na = na * 10 + (a[ia] - '0');
            ia++;
            sa = 1;
        }
        while (b[ib] >= '0' && b[ib] <= '9') {
            nb = nb * 10 + (b[ib] - '0');
            ib++;
            sb = 1;
        }
        if (na != nb)
            return na < nb ? -1 : 1;
        /* both exhausted or a suffix begins here */
        int suffix_a = a[ia] != '\0' && a[ia] != '.';
        int suffix_b = b[ib] != '\0' && b[ib] != '.';
        if (suffix_a && suffix_b) {
            /* compare suffixes lexically, then stop conservatively */
            int c = strcmp(a + ia, b + ib);
            if (c != 0)
                return c < 0 ? -1 : 1;
            return 0;
        }
        if (suffix_a != suffix_b)
            return suffix_a ? -1 : 1; /* "1.0.0-beta" < "1.0.0" */
        if (a[ia] == '\0' && b[ib] == '\0')
            return 0;
        if (sa == 0 && sb == 0 && a[ia] == '\0' && b[ib] == '\0')
            return 0;
        /* skip the '.' on both sides (or the one side that has it) */
        if (a[ia] == '.')
            ia++;
        if (b[ib] == '.')
            ib++;
        if (a[ia] == '\0' && b[ib] == '\0')
            return 0;
        if (sa == 0 && sb == 0)
            return 0;
    }
}

static int version_in_range(const char *v, const oc_osv_range *r) {
    if (!v)
        return 0;
    if (strcmp(r->introduced, "0") != 0 && oc_version_cmp(v, r->introduced) < 0)
        return 0;
    if (r->fixed && oc_version_cmp(v, r->fixed) >= 0)
        return 0;
    return 1;
}

int oc_osv_match(const oc_osv_snapshot *s, const char *name, const char *version,
                 oc_ecosystem eco) {
    if (!s || !name)
        return 0;
    int hit = 0;
    for (size_t r = 0; r < s->n_records; r++) {
        const oc_osv_record *rec = &s->records[r];
        for (size_t p = 0; p < rec->n_packages; p++) {
            const oc_osv_package *pkg = &rec->packages[p];
            if (pkg->eco != eco || strcmp(pkg->name, name) != 0)
                continue;
            if (!version)
                return 2; /* known name, unknown version: not guessed */
            for (size_t i = 0; i < pkg->n_ranges; i++) {
                if (version_in_range(version, &pkg->ranges[i])) {
                    hit = 1;
                    break;
                }
            }
            if (hit)
                return 1;
        }
    }
    return 0;
}

oc_code oc_osv_load(oc_osv_snapshot *s, const char *path, oc_arena *a, oc_status *st) {
    char *data = NULL;
    size_t len = 0;
    oc_code rc = oc_read_file(path, &data, &len, st);
    if (rc != OC_OK)
        return rc;
    oc_json *root = oc_json_parse(a, data, len, st);
    free(data);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_status_set(st, OC_ERR_USAGE, "OSV snapshot '%s' is not a JSON object", path);
        return OC_ERR_USAGE;
    }
    const oc_json *date = oc_json_get(root, "snapshot_date");
    s->snapshot_date = date && date->type == OC_JSON_STR ? date->str : NULL;
    const oc_json *records = oc_json_get(root, "records");
    if (!records || records->type != OC_JSON_ARR) {
        oc_status_set(st, OC_ERR_USAGE, "OSV snapshot '%s' lacks a \"records\" array", path);
        return OC_ERR_USAGE;
    }
    for (size_t i = 0; i < oc_json_size(records); i++) {
        const oc_json *rj = records->items[i];
        if (rj->type != OC_JSON_OBJ)
            continue;
        oc_osv_record rec;
        memset(&rec, 0, sizeof(rec));
        rec.id = oc_json_as_str(oc_json_get(rj, "id"), "");
        const oc_json *affected = oc_json_get(rj, "affected");
        if (affected && affected->type == OC_JSON_ARR) {
            rec.n_packages = oc_json_size(affected);
            rec.packages = (oc_osv_package *)calloc(rec.n_packages, sizeof(oc_osv_package));
            if (!rec.packages) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory loading OSV snapshot");
                return OC_ERR_ALLOC;
            }
            for (size_t p = 0; p < rec.n_packages; p++) {
                const oc_json *aj = affected->items[p];
                oc_osv_package *pkg = &rec.packages[p];
                if (aj->type != OC_JSON_OBJ)
                    continue;
                const oc_json *pj = oc_json_get(aj, "package");
                if (!pj || pj->type != OC_JSON_OBJ)
                    continue;
                pkg->name = oc_json_as_str(oc_json_get(pj, "name"), "");
                const char *eco = oc_json_as_str(oc_json_get(pj, "ecosystem"), "");
                if (strcmp(eco, "npm") == 0)
                    pkg->eco = OC_ECO_NPM;
                else if (strcmp(eco, "PyPI") == 0)
                    pkg->eco = OC_ECO_PYPI;
                else if (strcmp(eco, "Go") == 0)
                    pkg->eco = OC_ECO_GO;
                else if (strcmp(eco, "Maven") == 0)
                    pkg->eco = OC_ECO_MAVEN;
                else if (strcmp(eco, "NuGet") == 0)
                    pkg->eco = OC_ECO_NUGET; /* SPRINT-13 */
                else if (strcmp(eco, "crates.io") == 0)
                    pkg->eco = OC_ECO_CRATES; /* SPRINT-15 */
                else if (strcmp(eco, "rubygems") == 0)
                    pkg->eco = OC_ECO_RUBYGEMS; /* SPRINT-16 */
                else if (strcmp(eco, "packagist") == 0)
                    pkg->eco = OC_ECO_PACKAGIST; /* SPRINT-19 */
                else
                    pkg->eco = OC_ECO_NONE;
                const oc_json *ranges = oc_json_get(aj, "ranges");
                if (ranges && ranges->type == OC_JSON_ARR) {
                    size_t nr = oc_json_size(ranges);
                    if (nr > 0) {
                        pkg->ranges = (oc_osv_range *)calloc(nr, sizeof(oc_osv_range));
                        if (!pkg->ranges) {
                            oc_status_set(st, OC_ERR_ALLOC, "out of memory loading OSV snapshot");
                            return OC_ERR_ALLOC;
                        }
                        for (size_t ri = 0; ri < nr; ri++) {
                            const oc_json *rg = ranges->items[ri];
                            const oc_json *events = oc_json_get(rg, "events");
                            if (!events || events->type != OC_JSON_ARR)
                                continue;
                            oc_osv_range out_range;
                            memset(&out_range, 0, sizeof(out_range));
                            out_range.introduced = "0";
                            for (size_t e = 0; e < oc_json_size(events); e++) {
                                const oc_json *ev = events->items[e];
                                if (ev->type != OC_JSON_OBJ)
                                    continue;
                                const oc_json *in_ev = oc_json_get(ev, "introduced");
                                if (in_ev && in_ev->type == OC_JSON_STR)
                                    out_range.introduced = in_ev->str;
                                const oc_json *fx = oc_json_get(ev, "fixed");
                                if (fx && fx->type == OC_JSON_STR)
                                    out_range.fixed = fx->str;
                            }
                            pkg->ranges[pkg->n_ranges++] = out_range;
                        }
                    }
                }
            }
        }
        if (s->n_records == s->cap_records) {
            size_t ncap = s->cap_records ? s->cap_records * 2 : 16;
            oc_osv_record *grown = (oc_osv_record *)realloc(s->records, ncap * sizeof(*grown));
            if (!grown) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory loading OSV snapshot");
                return OC_ERR_ALLOC;
            }
            s->records = grown;
            s->cap_records = ncap;
        }
        s->records[s->n_records++] = rec;
    }
    return OC_OK;
}

/* ------------------------------------------------------------- secrets */

typedef struct oc_secret_pattern {
    const char *needle; /* literal byte prefix */
    size_t needle_len;
    int after_min;       /* min chars after the needle */
    int after_max;       /* max chars after the needle (-1 = unbounded) */
    const char *allowed; /* NULL, or the charset that must cover the run
                          * immediately after the needle; when the run
                          * length is fixed (after_min == after_max), the
                          * char following the run must NOT be in the
                          * charset (boundary) so a quoted 36-char token
                          * still matches inside string literals */
    oc_severity sev;
    const char *label;
} oc_secret_pattern;

/* Fixed, documented pattern list (supply.h). First match per pattern per
 * line wins; every match is byte-precise. */
static const oc_secret_pattern PATTERNS[] = {
    {"AKIA", 4, 16, 16, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", OC_SEV_ERROR, "AWS access key id"},
    {"-----BEGIN ", 11, 0, -1, NULL, OC_SEV_ERROR, "PEM key header"},
    {"ghp_", 4, 36, 36, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
     OC_SEV_ERROR, "GitHub personal access token"},
    {"xox", 3, 10, -1, NULL, OC_SEV_ERROR, "Slack token"},
};
#define OC_SECRET_N_PATTERNS 4

/* Generic secret-looking assignment: name contains one of the key words
 * and the value is a quoted literal of >= 12 chars. The literal must be
 * TERMINATED on the same line: an unterminated opening quote (the
 * apostrophe in prose like "the token's indentation is dependent on") is
 * not a literal and must not manufacture one out of the rest of the
 * line. Found on the pinned eslint corpus: six prose comment lines were
 * flagged as secret assignments because "token's" + a long tail looked
 * like a 12+ char quoted literal. Outputs the keyword and the literal
 * bounds so the caller can fold them into the fingerprint. */
static int secret_assignment_line(const char *line, size_t len, const char **key_out,
                                  size_t *key_len, const char **lit_out, size_t *lit_len) {
    static const char *const KEYWORDS[] = {"password", "secret", "api_key", "apikey", "token"};
    for (size_t k = 0; k < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); k++) {
        const char *hit = NULL;
        for (size_t i = 0; i + strlen(KEYWORDS[k]) <= len; i++) {
            if (memcmp(line + i, KEYWORDS[k], strlen(KEYWORDS[k])) == 0) {
                hit = line + i;
                break;
            }
        }
        if (!hit)
            continue;
        *key_out = KEYWORDS[k];
        *key_len = strlen(KEYWORDS[k]);
        /* quoted literal after the keyword? */
        for (const char *p = hit + *key_len; p < line + len; p++) {
            if (*p == '"' || *p == '\'') {
                char q = *p;
                const char *v = p + 1;
                const char *close = memchr(v, q, (size_t)(line + len - v));
                if (!close)
                    continue; /* unterminated opening: prose, not a literal */
                size_t vlen = (size_t)(close - v);
                if (vlen >= 12) {
                    *lit_out = v;
                    *lit_len = vlen;
                    return 1;
                }
                return 0; /* terminated but short: not a secret */
            }
        }
    }
    return 0;
}

oc_code oc_supply_scan_secrets(const char *path, const char *src, size_t len, oc_arena *a,
                               oc_finding_vec *out, oc_status *st) {
    (void)a;
    size_t line_start = 0;
    size_t lineno = 0;
    while (line_start <= len) {
        size_t line_end = line_start;
        while (line_end < len && src[line_end] != '\n')
            line_end++;
        size_t linelen = line_end - line_start;
        lineno++;
        const char *line = src + line_start;
        for (size_t pi = 0; pi < OC_SECRET_N_PATTERNS; pi++) {
            const oc_secret_pattern *pat = &PATTERNS[pi];
            for (size_t i = 0; i + pat->needle_len <= linelen; i++) {
                if (memcmp(line + i, pat->needle, pat->needle_len) != 0)
                    continue;
                size_t after = linelen - i - pat->needle_len;
                if (pat->allowed) {
                    /* charset run right after the needle decides; the
                     * remainder of the line after the token is not part
                     * of the shape (quotes, semicolons follow). */
                    size_t run = 0;
                    while (run < after && strchr(pat->allowed, line[i + pat->needle_len + run]))
                        run++;
                    if (run < (size_t)pat->after_min)
                        continue;
                    if (pat->after_max >= 0 && run != (size_t)pat->after_max)
                        continue; /* fixed-length token: boundary enforced */
                    if (pat->after_max < 0 && run > (size_t)pat->after_min + 96)
                        continue; /* runaway: not the documented shape */
                } else {
                    if (pat->after_max >= 0 && after < (size_t)pat->after_min)
                        continue;
                    if (pat->after_max >= 0 && after > (size_t)pat->after_max)
                        continue;
                }
                char *msg = oc_arena_sprintf(a,
                                             "Source text matches the %s pattern "
                                             "(deterministic supply scan)",
                                             pat->label);
                if (!msg) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
                    return OC_ERR_ALLOC;
                }
                char anchor[64];
                snprintf(anchor, sizeof(anchor), "secret:%s", pat->label);
                oc_finding_spec spec;
                memset(&spec, 0, sizeof(spec));
                spec.rule_id = OC_SUPPLY_SECRET_RULE;
                spec.rule_name = "secret-pattern";
                spec.severity = pat->sev;
                spec.file = path;
                spec.line = lineno;
                spec.col = i + 1;
                spec.end_line = lineno;
                spec.symbol = NULL;
                spec.message = msg;
                spec.fp_text = anchor;
                if (!oc_findings_push(out, a, &spec)) {
                    oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
                    return OC_ERR_ALLOC;
                }
                break; /* first match per pattern per line */
            }
        }
        const char *kw = NULL;
        size_t kw_len = 0;
        const char *lit = NULL;
        size_t lit_len = 0;
        if (secret_assignment_line(line, linelen, &kw, &kw_len, &lit, &lit_len)) {
            char *msg = oc_arena_sprintf(a,
                                         "Assignment to secret-looking name '%.*s' with a "
                                         "quoted literal (deterministic supply scan)",
                                         (int)kw_len, kw);
            if (!msg) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
                return OC_ERR_ALLOC;
            }
            /* Fold keyword and literal into the fingerprint: the old
             * constant made every generic assignment in a file share one
             * identity (even different keywords). Identical statements
             * (same keyword, same literal) still share one fingerprint
             * by design -- the location is deliberately excluded. */
            char *fp = oc_arena_sprintf(a, "secret:generic-assignment:%.*s=%.*s", (int)kw_len, kw,
                                        (int)lit_len, lit);
            if (!fp) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
                return OC_ERR_ALLOC;
            }
            oc_finding_spec spec;
            memset(&spec, 0, sizeof(spec));
            spec.rule_id = OC_SUPPLY_SECRET_RULE;
            spec.rule_name = "secret-pattern";
            spec.severity = OC_SEV_WARN;
            spec.file = path;
            spec.line = lineno;
            spec.col = 1;
            spec.end_line = lineno;
            spec.symbol = NULL;
            spec.message = msg;
            spec.fp_text = fp;
            if (!oc_findings_push(out, a, &spec)) {
                oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
                return OC_ERR_ALLOC;
            }
        }
        if (line_end >= len)
            break;
        line_start = line_end + 1;
        if (src[line_end] != '\n')
            break;
    }
    oc_status_clear(st);
    return OC_OK;
}
