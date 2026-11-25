/*
 * Breaking-change gate (WBS 4.6, M4 RC scope; signature diffs SPRINT-17
 * per the B3 decision, Option B typed-where-declared): graph diff
 * between two analysis roots (a synthetic version bump in CI). The
 * plan's "compat gate" compares the PUBLIC SURFACE of the two trees:
 *
 *   - removed named export        -> BREAKING (removed-export)
 *   - module file with exports    -> BREAKING (removed-module)
 *     gone from the current tree
 *   - function node gone from a   -> BREAKING (removed-function)
 *     public module (a file that
 *     still exists and still carries
 *     exports)
 *   - signature of a function     -> BREAKING / informational per the
 *     present on both sides         policy table (docs/SPRINT-17.md
 *                                 section 3): changed-param-count,
 *                                 added-param-default, param-reordered,
 *                                 changed-param-type, param-renamed,
 *                                 removed-overload, added-overload
 *   - added exports / functions / -> informational, never breaking
 *     files
 *
 * Honest scope notes: signature types compare as RAW source-order text
 * with no resolution or normalization; a parameter whose type is
 * undeclared on either side is never compared (NULL = not comparable,
 * never "equal"). Removals always break; an added parameter breaks
 * unless every added trailing parameter carries a default (languages
 * with defaults: JS family, Python, Ruby, C#). Java/C# overloads
 * compare per arity as a signature set (an arity fully removed is
 * BREAKING, a partially removed set is informational). Import-graph
 * usage is deliberately ignored: the gate is a pure surface diff, CI
 * consumers decide policy on the verdict.
 *
 * Determinism: both sides are analyzed with the SAME pinned grammars;
 * changes are sorted (kind asc, path asc, symbol asc, detail asc) with
 * one finding per changed symbol (per arity for overload families).
 * Exit code 1 when the verdict is BREAKING (CI gate semantics), 0 when
 * SAFE, 2 on tool errors. Report: omnicheck.compat_report/v1 (v0
 * retired; every change object may carry a one-line `detail`).
 */
#ifndef OC_COMPAT_H
#define OC_COMPAT_H

#include "arena.h"
#include "common.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the compat gate: baseline/current roots, shared packs dir. The
 * JSON verdict is written to `out`. Returns the exit code: 0 SAFE, 1
 * BREAKING, 2 tool error (st set). */
oc_code oc_compat_run(const char *baseline_root, const char *current_root, const char *packs_dir,
                      FILE *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_COMPAT_H */
