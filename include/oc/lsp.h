/*
 * LSP server surface (plan WBS 3.3, Sprint 4 / M3 Beta).
 *
 * `oc lsp` speaks LSP 3.17 over stdio (JSON-RPC 2.0 with Content-Length
 * framing) so OmniCheck diagnostics and document symbols are visible in
 * an ordinary editor session. The server is a thin protocol adapter over
 * the shared scan pipeline (oc_scan_run): every trigger re-runs the full
 * deterministic analysis of the workspace root and publishes findings
 * for the affected document. Nothing here analyzes anything itself.
 *
 * v0 contract (honest scopes recorded in docs/ARCHITECTURE.md):
 *   - Diagnostics reflect the ON-DISK state. didOpen/didChange/didSave
 *     each trigger a fresh scan; unsaved buffer text is not analyzed
 *     (descope: buffer-state analysis). Editors that save before review
 *     get exact diagnostics; unsaved edits only move them on save.
 *   - Positions carry byte columns (tree-sitter), not UTF-16 code units.
 *   - Deterministic: identical trees produce identical notifications.
 */
#ifndef OC_LSP_H
#define OC_LSP_H

#include "common.h"
#include "json.h"
#include "pass.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_lsp_options {
    const char *packs_dir;
    const oc_config *cfg; /* pass options; must outlive the call */
} oc_lsp_options;

/* Runs the LSP session until the client sends `exit` or stdin EOF.
 * Returns the process exit code: 0 after a clean shutdown, 1 on `exit`
 * without a preceding `shutdown` (LSP spec), 2 on fatal IO/protocol
 * errors. Protocol messages go to `out`; logs (if any) go to stderr. */
int oc_lsp_run(const oc_lsp_options *opt, FILE *in, FILE *out, oc_status *st);

/* Framing helpers, exposed for tests. A message is one or more
 * `Name: value` header lines (CRLF or LF) followed by a blank line and
 * exactly Content-Length body bytes. */
oc_code oc_lsp_read_message(FILE *in, char **out_body, size_t *out_len, oc_arena *a, oc_status *st);
oc_code oc_lsp_write_message(FILE *out, const char *body, size_t len);

/* file:// URI <-> filesystem path (percent-encoded, minimal and exact for
 * the byte set LSP clients actually send). Returns arena-owned strings. */
char *oc_lsp_uri_from_path(oc_arena *a, const char *path);
char *oc_lsp_path_from_uri(oc_arena *a, const char *uri);

#ifdef __cplusplus
}
#endif
#endif /* OC_LSP_H */
