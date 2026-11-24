#include "oc/buildtag.h"

#include <string.h>

/* ------------------------------------------------------- vocabularies -- */

static const char *const GOOS_SET[] = {
    "aix",  "android", "darwin",  "dragonfly", "freebsd", "illumos", "ios",     "js",  "linux",
    "nacl", "netbsd",  "openbsd", "plan9",     "solaris", "wasip1",  "windows", "zos", NULL};

static const char *const GOARCH_SET[] = {
    "386",      "amd64", "amd64p32", "arm",     "arm64", "loong64", "mips", "mipsle", "mips64",
    "mips64le", "ppc64", "ppc64le",  "riscv64", "s390x", "sparc64", "wasm", NULL};

/* The platforms the go tool counts as unix (go/build unixOS; nacl/js/
 * windows/zos are NOT unix). */
static const char *const UNIX_SET[] = {"aix",     "android", "darwin", "dragonfly", "freebsd",
                                       "illumos", "ios",     "linux",  "netbsd",    "openbsd",
                                       "plan9",   "solaris", NULL};

static int in_set(const char *const *set, const char *tag) {
    for (size_t i = 0; set[i]; i++) {
        if (strcmp(set[i], tag) == 0)
            return 1;
    }
    return 0;
}

static int tag_true(const char *tag, const char *goos, const char *goarch, const char *compiler,
                    int cgo, const char *const *tags, size_t n_tags) {
    if (in_set(GOOS_SET, tag))
        return goos && strcmp(tag, goos) == 0;
    if (in_set(GOARCH_SET, tag))
        return goarch && strcmp(tag, goarch) == 0;
    if (strcmp(tag, "unix") == 0)
        return goos && in_set(UNIX_SET, goos);
    if (strcmp(tag, "cgo") == 0)
        return cgo != 0;
    if (strcmp(tag, "gc") == 0 || strcmp(tag, "gccgo") == 0)
        return compiler && strcmp(tag, compiler) == 0;
    for (size_t i = 0; i < n_tags; i++) {
        if (tags[i] && strcmp(tag, tags[i]) == 0)
            return 1;
    }
    return 0; /* release tags, custom -tags, ignore: FALSE by contract */
}

/* ---------------------------------------------------- +build parsing -- */

/* One legacy option: comma-separated AND'ed atoms, each optionally
 * negated. `s..e` is the option text. */
static int plusbuild_option(const char *s, const char *e, const char *goos, const char *goarch,
                            const char *compiler, int cgo, const char *const *tags, size_t n_tags) {
    int ok = 1; /* empty option: vacuously true, keep the file */
    const char *p = s;
    while (p < e) {
        const char *comma = memchr(p, ',', (size_t)(e - p));
        const char *atom_end = comma ? comma : e;
        int neg = 0;
        const char *atom = p;
        if (atom < atom_end && *atom == '!') {
            neg = 1;
            atom++;
        }
        /* an empty atom between commas: not a tag, ignore it honestly */
        if (atom < atom_end) {
            char tag[64];
            size_t tl = (size_t)(atom_end - atom);
            if (tl < sizeof(tag)) {
                memcpy(tag, atom, tl);
                tag[tl] = '\0';
                int truth = tag_true(tag, goos, goarch, compiler, cgo, tags, n_tags);
                if (neg)
                    truth = !truth;
                if (!truth)
                    ok = 0; /* one AND'ed term false: option unsatisfied */
            }
        }
        if (!comma)
            break;
        p = comma + 1;
    }
    return ok;
}

/* ------------------------------------------------- //go:build parsing -- */

typedef struct {
    const char *p; /* cursor */
    const char *end;
} bscan;

static void bskip_ws(bscan *s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t'))
        s->p++;
}

/* identifier: letters, digits, '_', '.' (go1.21+ allows dots in tags) */
static int btoken_ident(bscan *s, char *out, size_t cap) {
    size_t n = 0;
    while (s->p < s->end) {
        char c = *s->p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.'))
            break;
        if (n + 1 >= cap)
            return 0; /* too long: malformed, caller treats as no-constraint */
        out[n++] = c;
        s->p++;
    }
    out[n] = '\0';
    return n > 0;
}

/* forward: or_expr */
static int bexpr_or(bscan *s, const char *goos, const char *goarch, const char *compiler, int cgo,
                    const char *const *tags, size_t n_tags, int *out);

static int bexpr_atom(bscan *s, const char *goos, const char *goarch, const char *compiler, int cgo,
                      const char *const *tags, size_t n_tags, int *out) {
    bskip_ws(s);
    if (s->p >= s->end)
        return 0;
    if (*s->p == '!') {
        s->p++;
        int v = 0;
        if (!bexpr_atom(s, goos, goarch, compiler, cgo, tags, n_tags, &v))
            return 0;
        *out = !v;
        return 1;
    }
    if (*s->p == '(') {
        s->p++;
        if (!bexpr_or(s, goos, goarch, compiler, cgo, tags, n_tags, out))
            return 0;
        bskip_ws(s);
        if (s->p < s->end && *s->p == ')') {
            s->p++;
            return 1;
        }
        return 0; /* unterminated paren: malformed */
    }
    char tag[64];
    if (!btoken_ident(s, tag, sizeof(tag)))
        return 0;
    *out = tag_true(tag, goos, goarch, compiler, cgo, tags, n_tags);
    return 1;
}

static int bexpr_and(bscan *s, const char *goos, const char *goarch, const char *compiler, int cgo,
                     const char *const *tags, size_t n_tags, int *out) {
    if (!bexpr_atom(s, goos, goarch, compiler, cgo, tags, n_tags, out))
        return 0;
    for (;;) {
        bskip_ws(s);
        if (s->p + 2 <= s->end && s->p[0] == '&' && s->p[1] == '&') {
            s->p += 2;
            int v = 0;
            if (!bexpr_atom(s, goos, goarch, compiler, cgo, tags, n_tags, &v))
                return 0;
            *out = *out && v;
            continue;
        }
        return 1;
    }
}

static int bexpr_or(bscan *s, const char *goos, const char *goarch, const char *compiler, int cgo,
                    const char *const *tags, size_t n_tags, int *out) {
    if (!bexpr_and(s, goos, goarch, compiler, cgo, tags, n_tags, out))
        return 0;
    for (;;) {
        bskip_ws(s);
        if (s->p + 2 <= s->end && s->p[0] == '|' && s->p[1] == '|') {
            s->p += 2;
            int v = 0;
            if (!bexpr_and(s, goos, goarch, compiler, cgo, tags, n_tags, &v))
                return 0;
            *out = *out || v;
            continue;
        }
        return 1;
    }
}

/* The full //go:build expression: parse to end-of-expression (all input
 * consumed except trailing whitespace). */
static int gobuild_expr(const char *expr, const char *expr_end, const char *goos,
                        const char *goarch, const char *compiler, int cgo, const char *const *tags,
                        size_t n_tags, int *out) {
    bscan s = {expr, expr_end};
    if (!bexpr_or(&s, goos, goarch, compiler, cgo, tags, n_tags, out))
        return 0;
    bskip_ws(&s);
    return s.p == s.end; /* trailing garbage: malformed */
}

/* -------------------------------------------------------- header scan -- */

int oc_go_buildtag_excluded(const char *data, size_t len, const char *goos, const char *goarch,
                            const char *compiler, int cgo, const char *const *tags, size_t n_tags) {
    const char *gob = NULL; /* first //go:build expression */
    const char *gob_end = NULL;
    /* legacy // +build lines: evaluated on the fly, OR'ed; starts false so
     * an unconstrained file stays in the set */
    int legacy_value = 0;
    int have_legacy = 0;

    size_t i = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && data[eol] != '\n')
            eol++;
        /* trim leading whitespace */
        size_t s = i;
        while (s < eol && (data[s] == ' ' || data[s] == '\t' || data[s] == '\r'))
            s++;
        size_t l2 = eol;
        while (l2 > s && (data[l2 - 1] == '\r'))
            l2--;
        if (l2 > s) {
            if (l2 - s >= 2 && data[s] == '/' && data[s + 1] == '/') {
                const char *body = data + s + 2;
                const char *bend = data + l2;
                if ((size_t)(bend - body) >= 9 && memcmp(body, "go:build ", 9) == 0) {
                    if (!gob) {
                        gob = body + 9;
                        gob_end = bend;
                    }
                } else if ((size_t)(bend - body) >= 7 && memcmp(body, " +build", 7) == 0 &&
                           (bend - body == 7 || body[7] == ' ' || body[7] == '\t')) {
                    /* the // +build line: one or more space-separated
                     * options, OR'ed */
                    have_legacy = 1;
                    const char *p = body + 7;
                    while (p < bend) {
                        while (p < bend && (*p == ' ' || *p == '\t'))
                            p++;
                        const char *opt = p;
                        while (p < bend && *p != ' ' && *p != '\t')
                            p++;
                        if (p > opt &&
                            plusbuild_option(opt, p, goos, goarch, compiler, cgo, tags, n_tags))
                            legacy_value = 1;
                    }
                }
                /* any other //-comment: part of the header, keep scanning */
            } else {
                /* the first non-blank, non-line-comment line (a block
                 * comment, the package clause or code) ends the header
                 * region */
                break;
            }
        }
        i = eol + 1;
    }

    if (gob) {
        int v = 0;
        if (gobuild_expr(gob, gob_end, goos, goarch, compiler, cgo, tags, n_tags, &v))
            return v ? 0 : 1; /* go:build wins over +build when both exist */
        return 0;             /* malformed expression: over-report, analyze */
    }
    if (have_legacy)
        return legacy_value ? 0 : 1;
    return 0; /* no constraint: analyze as written */
}
