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

#ifdef __cplusplus
}
#endif
#endif /* OC_REPORT_H */
