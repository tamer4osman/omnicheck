/*
 * Supply-chain tier (WBS 4.2, M4 RC scope): dependency health against a
 * VENDORED OSV snapshot with a visible snapshot date (plan Ch4 4.7:
 * external data is vendored into the run so an offline run today
 * reproduces an offline run next month), licence checks on manifests,
 * and deterministic secret-pattern scanning over analyzed sources.
 *
 * Rules emitted (registered in pass.c like every other rule):
 *   OC009 dep-vuln   -- a pinned dependency matched a record in the
 *                       vendored OSV snapshot (severity error). An
 *                       unpinned requirement (requirements.txt without
 *                       "==") is reported as warn: it cannot be matched
 *                       honestly against any snapshot.
 *   OC010 licence    -- a manifest that declares no licence (package.json
 *                       without a non-empty "license" string, or
 *                       pyproject.toml without a `license = "..."` line;
 *                       go.mod carries NO licence field, so the module
 *                       root named by the manifest is probed for a
 *                       licence FILE among the discovered files --
 *                       SPRINT-11, docs/LANGUAGES.md §Go.6; pom.xml
 *                       licence from the project <license> clause,
 *                       SPRINT-12; .csproj licence from the project's
 *                       PackageLicenseExpression / PackageLicenseFile /
 *                       the project-root licence FILE, SPRINT-13).
 *   OC011 secret     -- credential-shaped patterns in analyzed sources
 *                       (AWS AKIA key id, PEM private key header, GitHub
 *                       ghp_ token, Slack xox token: error; generic
 *                       assignment to a secret-looking name: warn).
 *
 * Manifest-level findings anchor at line 1 of the manifest with the
 * dependency/symbol named in the message: the JSON parser does not track
 * line numbers and the report must not guess (requirements.txt and
 * go.mod are line-scanned and DO carry real line numbers).
 *
 * Version comparison is deliberately conservative and documented: npm
 * specifiers are matched only when EXACTLY pinned (no ^ ~ * x ranges);
 * PyPI requirements only when pinned with "=="; go.mod requires always
 * carry an exact semantic version, so every require is a pin (SPRINT-11;
 * version ranges do not exist in go.mod); Maven pins are exact literals
 * in `group:artifact` form -- version ranges, ${property} references,
 * LATEST/RELEASE and -SNAPSHOT are NOT pins (SPRINT-12). NuGet pins are
 * exact literal PackageReference versions -- property references and
 * floating/ranged versions are NOT pins (SPRINT-13). Cargo pins are the
 * EXACT requirement form `=x.y.z` -- caret/tilde/star/range requirements
 * are NOT pins, unpinned = warn, never guessed (SPRINT-15; Cargo's own
 * semantics). RubyGems pins are the Gemfile.lock GEM>specs resolved
 * entries and the Gemfile bare-version / `=` spec strings -- `~>`, `>=`
 * and every other comparator range is NOT a pin, unpinned = warn, never
 * guessed (SPRINT-16; bundler's own semantics). Versions compare as
 * dotted numeric segments (missing segment = 0); a prerelease suffix
 * sorts before the plain release. Anything that cannot be compared is
 * skipped, never guessed.
 */
#ifndef OC_SUPPLY_H
#define OC_SUPPLY_H

#include "arena.h"
#include "common.h"
#include "finding.h"
#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum oc_manifest_kind {
    OC_MANIFEST_PACKAGE_JSON = 0,
    OC_MANIFEST_PYPROJECT,
    OC_MANIFEST_REQUIREMENTS,
    OC_MANIFEST_POM,           /* SPRINT-12 (docs/LANGUAGES.md Java section) */
    OC_MANIFEST_GOMOD,         /* SPRINT-11 (docs/LANGUAGES.md §Go.6) */
    OC_MANIFEST_CSPROJ,        /* SPRINT-13 (docs/LANGUAGES.md C# section) */
    OC_MANIFEST_GRADLE,        /* SPRINT-14: build.gradle(.kts) bounded reader */
    OC_MANIFEST_PKGS_CONFIG,   /* SPRINT-14: legacy NuGet packages.config */
    OC_MANIFEST_PKGS_LOCK,     /* SPRINT-14: packages.lock.json (resolved pins) */
    OC_MANIFEST_CARGO_TOML,    /* SPRINT-15: Cargo.toml (docs/LANGUAGES.md Rust section 6) */
    OC_MANIFEST_CARGO_LOCK,    /* SPRINT-15: Cargo.lock (resolved pins) */
    OC_MANIFEST_GEMFILE,       /* SPRINT-16: Gemfile (docs/LANGUAGES.md Ruby section 6) */
    OC_MANIFEST_GEMFILE_LOCK,  /* SPRINT-16: Gemfile.lock (resolved pins) */
    OC_MANIFEST_COMPOSER_JSON, /* SPRINT-19: composer.json (docs/LANGUAGES.md PHP section 6) */
    OC_MANIFEST_COMPOSER_LOCK  /* SPRINT-19: composer.lock (resolved pins) */
} oc_manifest_kind;

const char *oc_manifest_kind_str(oc_manifest_kind k);

typedef enum oc_ecosystem {
    OC_ECO_NPM = 0,
    OC_ECO_PYPI,
    OC_ECO_GO,        /* SPRINT-11 (docs/LANGUAGES.md §Go.6) */
    OC_ECO_MAVEN,     /* SPRINT-12 (docs/LANGUAGES.md Java section) */
    OC_ECO_NUGET,     /* SPRINT-13 (docs/LANGUAGES.md C# section) */
    OC_ECO_CRATES,    /* SPRINT-15: crates.io (docs/LANGUAGES.md Rust section 6) */
    OC_ECO_RUBYGEMS,  /* SPRINT-16: rubygems (docs/LANGUAGES.md Ruby section 6) */
    OC_ECO_PACKAGIST, /* SPRINT-19: packagist (docs/LANGUAGES.md PHP section 6) */
    OC_ECO_NONE
} oc_ecosystem;

const char *oc_ecosystem_str(oc_ecosystem e);

typedef struct oc_dep {
    const char *name;
    const char *version; /* NULL when unpinned */
    size_t line;         /* requirements.txt and go.mod: declaration line
                          * (1-based); JSON/TOML/XML manifests: 1 (parser
                          * does not track line numbers; never guessed). */
    int pinned;
} oc_dep;

typedef struct oc_manifest {
    const char *path;
    oc_manifest_kind kind;
    oc_ecosystem eco;
    oc_dep *deps;
    size_t n_deps;
    size_t cap_deps;
    const char *license; /* NULL = no licence declared */
} oc_manifest;

typedef struct oc_manifest_vec {
    oc_manifest *items;
    size_t n;
    size_t cap;
} oc_manifest_vec;

void oc_manifests_init(oc_manifest_vec *v);
void oc_manifests_destroy(oc_manifest_vec *v);

/* One manifest that could not be read or parsed honestly (named, never
 * silent, BUGFIX N-2): it is SKIPPED -- the scan continues without its
 * licence and dependency data -- and the skip travels to the report
 * meta ("manifests_skipped" + the named "manifest_skip" list). One
 * corrupt package.json in a large tree must never veto the analysis of
 * every other file; hiding it would be equally wrong, so the skip is
 * recorded with its reason and reported. */
typedef struct oc_manifest_skip {
    const char *path;
    const char *reason;
} oc_manifest_skip;

typedef struct oc_manifest_skip_vec {
    oc_manifest_skip *items;
    size_t n;
    size_t cap;
} oc_manifest_skip_vec;

void oc_manifest_skips_init(oc_manifest_skip_vec *v);
void oc_manifest_skips_destroy(oc_manifest_skip_vec *v);

/* Scans the DISCOVERED file list for manifest basenames (package.json,
 * pyproject.toml, requirements.txt, go.mod, pom.xml, .csproj,
 * Cargo.toml, Cargo.lock) and parses
 * the ones found. All
 * strings live in the arena. With a non-NULL `skips` vec, unreadable or
 * unparseable manifests are RECORDED there (path + reason) and the run
 * continues; only out-of-memory stays fatal. With NULL, the legacy
 * fail-loud contract holds (any bad manifest fails the whole load).
 * BUGFIX N-2: `oc check` passes a skip vec so a single corrupt manifest
 * can no longer abort a scan of thousands of files; the skip is named
 * in the report meta, never silent. */
oc_code oc_supply_load_manifests_ex(oc_manifest_vec *out, oc_manifest_skip_vec *skips,
                                    const char *const *files, size_t n_files, oc_arena *a,
                                    oc_status *st);

/* Legacy fail-loud wrapper (unchanged contract for direct API users):
 * oc_supply_load_manifests_ex with skips == NULL. */
oc_code oc_supply_load_manifests(oc_manifest_vec *out, const char *const *files, size_t n_files,
                                 oc_arena *a, oc_status *st);

/* Vendored OSV snapshot (omnicheck.osv_snapshot/v0): {"snapshot_date":
 * "...", "records": [ ...OSV-shaped records... ]}. The snapshot date is
 * echoed into the report meta so the data age is visible in output. */
typedef struct oc_osv_range {
    const char *introduced; /* "0" = since the beginning */
    const char *fixed;      /* NULL = no known fix */
} oc_osv_range;

typedef struct oc_osv_package {
    const char *name;
    oc_ecosystem eco;
    oc_osv_range *ranges;
    size_t n_ranges;
} oc_osv_package;

typedef struct oc_osv_record {
    const char *id;
    oc_osv_package *packages;
    size_t n_packages;
} oc_osv_record;

typedef struct oc_osv_snapshot {
    const char *snapshot_date;
    oc_osv_record *records;
    size_t n_records;
    size_t cap_records;
} oc_osv_snapshot;

void oc_osv_init(oc_osv_snapshot *s);
void oc_osv_destroy(oc_osv_snapshot *s);

oc_code oc_osv_load(oc_osv_snapshot *s, const char *path, oc_arena *a, oc_status *st);

/* Dotted numeric version compare (negative = a<b). Prerelease suffixes
 * sort before the plain release ("1.0.0-beta" < "1.0.0"). */
int oc_version_cmp(const char *a, const char *b);

/* 1 when (name, version, ecosystem) matches some range in the snapshot;
 * 2 when the dependency exists in the snapshot but its version could not
 * be compared (unpinned); 0 when no record matches. */
int oc_osv_match(const oc_osv_snapshot *s, const char *name, const char *version, oc_ecosystem eco);

/* Secret-pattern scan over one source buffer. Findings are appended to
 * out (rule OC011); at most one finding per pattern per line, first
 * match wins. Deterministic: fixed patterns, fixed order, first-match. */
oc_code oc_supply_scan_secrets(const char *path, const char *src, size_t len, oc_arena *a,
                               oc_finding_vec *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_SUPPLY_H */
