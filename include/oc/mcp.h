/*
 * MCP server surface (plan WBS 3.4, Sprint 4 / M3 Beta).
 *
 * `oc mcp` speaks the Model Context Protocol stdio transport (JSON-RPC
 * 2.0, newline-delimited messages) so a coding agent can list and call
 * OmniCheck tools: run the deterministic scan, query findings, list the
 * rule registry. Like the LSP adapter, this file converts shapes only --
 * every tool call runs the shared scan pipeline (oc_scan_run).
 *
 * v0 contract:
 *   - tools/list always returns the three tools in fixed order with
 *     fixed input schemas; tools/call returns deterministic text
 *     content (the JSON report / filtered findings / rule registry).
 *   - Unknown request methods are JSON-RPC -32601 errors; unknown tool
 *     names are -32602 (protocol-level); scan failures inside a known
 *     tool are execution errors (result with isError=true, fail-open
 *     for the agent, never a crash).
 *   - The session ends when stdin closes (exit 0) -- the client owns
 *     the lifecycle, per MCP.
 */
#ifndef OC_MCP_H
#define OC_MCP_H

#include "common.h"
#include "pass.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oc_mcp_options {
    const char *packs_dir;
    const oc_config *cfg; /* base pass options; must outlive the call */
} oc_mcp_options;

/* Runs the MCP session until stdin EOF. Returns the process exit code
 * (0 normal, 2 fatal IO). Protocol goes to `out`; logs to stderr. */
int oc_mcp_run(const oc_mcp_options *opt, FILE *in, FILE *out, oc_status *st);

#ifdef __cplusplus
}
#endif
#endif /* OC_MCP_H */
