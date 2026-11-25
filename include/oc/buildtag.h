/*
 * Go build-constraint evaluation (SPRINT-14, docs/LANGUAGES.md §Go.8).
 *
 * The §Go.8 descope ("no build-constraint evaluation") is closed with a
 * DECLARED, deterministic build context: the go pack declares the analysis
 * target (goos/goarch/compiler/cgo/extra tags) in pack.json -- pack data,
 * not code, not the analysis host's uname (byte-identical reports must not
 * depend on the machine that runs the analysis).
 *
 * Both directive forms are evaluated, exactly as contracted:
 *   - the modern //go:build EXPR line (identifiers joined by || / &&, !,
 *     parentheses; the FIRST such line before the package clause wins);
 *   - the legacy // +build lines (space-separated options are OR'ed,
 *     comma-separated tags within one option are AND'ed, ! negates a tag;
 *     multiple lines are OR'ed). When BOTH forms are present the
 *     //go:build line wins (the go1.17+ semantics).
 *
 * Tag truth is bounded: a GOOS or GOARCH literal from the standard
 * vocabularies compares against the declared context, `unix` derives from
 * the declared GOOS, `gc`/`gccgo` compare against the declared compiler,
 * `cgo` is the declared cgo bit, declared extra tags compare literally;
 * ANY OTHER tag (release tags go1.x, custom -tags, ignore) is FALSE.
 *
 * Honesty direction: the header region ends at the first line that is not
 * blank and not a line comment (a block comment or code ends it); a
 * malformed or unreadable expression is treated as NO constraint -- the
 * file stays in the analyzed set. A false negative here over-reports (the
 * contract's preferred direction); it never hides code on a parser
 * weakness.
 */
#ifndef OC_BUILDTAG_H
#define OC_BUILDTAG_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the file's build-constraint directives EXCLUDE it under the
 * declared context, 0 when it stays in the analyzed set (including every
 * uncertain case -- see the honesty note above). */
int oc_go_buildtag_excluded(const char *data, size_t len, const char *goos, const char *goarch,
                            const char *compiler, int cgo, const char *const *tags, size_t n_tags);

#ifdef __cplusplus
}
#endif
#endif /* OC_BUILDTAG_H */
