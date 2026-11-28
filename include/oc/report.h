/*
 * Shared report renderers (Sprint 4). The `oc check` JSON and human
 * reports are the report contract of the tool; the CLI, the LSP server
 * (diagnostics), and the MCP server (omnicheck_scan) all consume the
 * same scan result, so the report is written exactly once and can never
 * drift between surfaces.
 */
#ifndef OC_REPORT_H
#define OC_REPORT_H

#include "json.h"
#include "scan.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Deterministic JSON report (omnicheck.report/v0). Field order is the
 * committed schema -- do not reorder. */
void oc_report_write_json(oc_jsonw *w, const oc_scan_result *res);

/* Deterministic grouped text report (rule sections sorted by rule id,
 * findings in the global (file, line, col, rule) order within each
 * section, parse errors as a named section, no timestamps). */
void oc_report_write_human(FILE *out, const oc_scan_result *res);

/* T6: kernel-minified Markdown report (omnicheck.report.llm/v0) -- the
 * token-efficient rendering for LLM consumers. Every prose field is
 * minified with the vendored kernel's own Markdown minifier (odk_minify)
 * and the document ends with a kernel token-estimate block
 * (odk_token_report: raw vs compressed tokens, reduction percent). Same
 * verdict and exit-code policy as the JSON render (handled by the CLI);
 * byte-stable; only emitted when --format llm was requested, so every
 * other run stays byte-identical. Requires the embedded kernel: builds
 * without it refuse loud via oc_report_llm_available() == 0. */
void oc_report_write_llm(FILE *out, const oc_scan_result *res);

/* 1 when --format llm can actually minify (embedded kernel linked in). */
int oc_report_llm_available(void);

#ifdef __cplusplus
}
#endif
#endif /* OC_REPORT_H */
