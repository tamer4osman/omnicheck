#include "oc/mcp.h"

#include <stdlib.h>
#include <string.h>

#include "oc/arena.h"
#include "oc/finding.h"
#include "oc/json.h"
#include "oc/report.h"
#include "oc/scan.h"
#include "oc/str.h"

/*
 * Model Context Protocol adapter (see oc/mcp.h). Three tools, all built
 * on the shared scan pipeline and the shared report contract:
 *   omnicheck_scan     -- the full deterministic JSON report as text
 *   omnicheck_findings -- filtered, capped findings query
 *   omnicheck_rules    -- the built-in rule registry
 */

#define OC_MCP_DEFAULT_LIMIT 100

typedef struct mcp_state {
    FILE *out;
    const oc_mcp_options *opt;
} mcp_state;

static void write_json_id(oc_jsonw *w, const oc_json *id) {
    if (!id) {
        oc_jsonw_null(w);
        return;
    }
    switch (id->type) {
    case OC_JSON_STR:
        oc_jsonw_str(w, id->str);
        break;
    case OC_JSON_NUM:
        if (id->number == (double)(long long)id->number)
            oc_jsonw_int(w, (long long)id->number);
        else
            oc_jsonw_float(w, id->number);
        break;
    case OC_JSON_BOOL:
        oc_jsonw_bool(w, id->boolean);
        break;
    default:
        oc_jsonw_null(w);
        break;
    }
}

/* Emits one JSON-RPC response line; `fill` writes the result/error body
 * as a complete JSON value under result_key. */
static void send_json(FILE *out, const oc_json *id, const char *result_key,
                      void (*fill)(oc_jsonw *w, void *ctx), void *ctx) {
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "jsonrpc");
    oc_jsonw_str(&w, "2.0");
    oc_jsonw_key(&w, "id");
    write_json_id(&w, id);
    oc_jsonw_key(&w, result_key);
    fill(&w, ctx);
    oc_jsonw_obj_end(&w);
    if (w.buf.data) {
        fputs(w.buf.data, out);
        fputc('\n', out);
        fflush(out);
    }
    oc_jsonw_destroy(&w);
}

static void send_error(FILE *out, const oc_json *id, int code, const char *message) {
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "jsonrpc");
    oc_jsonw_str(&w, "2.0");
    oc_jsonw_key(&w, "id");
    write_json_id(&w, id);
    oc_jsonw_key(&w, "error");
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "code");
    oc_jsonw_int(&w, code);
    oc_jsonw_key(&w, "message");
    oc_jsonw_str(&w, message);
    oc_jsonw_obj_end(&w);
    oc_jsonw_obj_end(&w);
    if (w.buf.data) {
        fputs(w.buf.data, out);
        fputc('\n', out);
        fflush(out);
    }
    oc_jsonw_destroy(&w);
}

/* Wraps a plain text payload as an MCP tool-result content block. */
static void fill_content(oc_jsonw *w, void *ctx) {
    const char *text = (const char *)ctx;
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "content");
    oc_jsonw_arr_begin(w);
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "text");
    oc_jsonw_key(w, "text");
    oc_jsonw_str(w, text);
    oc_jsonw_obj_end(w);
    oc_jsonw_arr_end(w);
    oc_jsonw_obj_end(w);
}

static void fill_empty(oc_jsonw *w, void *ctx) {
    (void)ctx;
    oc_jsonw_obj_begin(w);
    oc_jsonw_obj_end(w);
}

static void fill_initialize(oc_jsonw *w, void *ctx) {
    const char *client_version = (const char *)ctx;
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "protocolVersion");
    oc_jsonw_str(w, client_version && client_version[0] ? client_version : "2024-11-05");
    oc_jsonw_key(w, "capabilities");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "tools");
    oc_jsonw_obj_begin(w);
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
    oc_jsonw_key(w, "serverInfo");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "name");
    oc_jsonw_str(w, "omnicheck");
    oc_jsonw_key(w, "version");
    oc_jsonw_str(w, OC_VERSION_STRING);
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
}

/* --------------------------------------------------------- schemas */

static void write_str_item_array(oc_jsonw *w, const char *key, const char *desc) {
    oc_jsonw_key(w, key);
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "array");
    oc_jsonw_key(w, "items");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "string");
    oc_jsonw_obj_end(w);
    if (desc) {
        oc_jsonw_key(w, "description");
        oc_jsonw_str(w, desc);
    }
    oc_jsonw_obj_end(w);
}

static void write_str_field(oc_jsonw *w, const char *key, const char *desc) {
    oc_jsonw_key(w, key);
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "string");
    if (desc) {
        oc_jsonw_key(w, "description");
        oc_jsonw_str(w, desc);
    }
    oc_jsonw_obj_end(w);
}

static void write_scan_schema(oc_jsonw *w) {
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "object");
    oc_jsonw_key(w, "properties");
    oc_jsonw_obj_begin(w);
    write_str_item_array(w, "paths", "Analysis roots (default: the current working directory)");
    write_str_field(w, "packs_dir", "Language pack directory (declarative packs)");
    oc_jsonw_key(w, "config");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "object");
    oc_jsonw_key(w, "additionalProperties");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "string");
    oc_jsonw_obj_end(w);
    oc_jsonw_key(w, "description");
    oc_jsonw_str(w, "Pass options as KEY=VALUE strings, e.g. OC001.max_lines=40");
    oc_jsonw_obj_end(w);
    write_str_item_array(w, "coverage",
                         "Istanbul / coverage.py JSON files (enables OC007 CRAP-ms)");
    oc_jsonw_key(w, "triage");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "boolean");
    oc_jsonw_key(w, "description");
    oc_jsonw_str(w, "Enable the OmniDecide rank-only sidecar (fail-open)");
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
}

static void write_findings_schema(oc_jsonw *w) {
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "object");
    oc_jsonw_key(w, "properties");
    oc_jsonw_obj_begin(w);
    write_str_item_array(w, "paths", "Analysis roots (default: the current working directory)");
    write_str_field(w, "packs_dir", "Language pack directory");
    oc_jsonw_key(w, "config");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "object");
    oc_jsonw_key(w, "additionalProperties");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "string");
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
    write_str_field(w, "rule", "Filter by rule id, e.g. OC004");
    write_str_field(w, "file_contains", "Only findings whose file path contains this substring");
    oc_jsonw_key(w, "severity");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "string");
    oc_jsonw_key(w, "enum");
    oc_jsonw_arr_begin(w);
    oc_jsonw_str(w, "info");
    oc_jsonw_str(w, "warn");
    oc_jsonw_str(w, "error");
    oc_jsonw_arr_end(w);
    oc_jsonw_obj_end(w);
    oc_jsonw_key(w, "limit");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "integer");
    oc_jsonw_key(w, "description");
    oc_jsonw_str(w, "Maximum findings returned (first N in canonical order)");
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
}

static void write_rules_schema(oc_jsonw *w) {
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "object");
    oc_jsonw_key(w, "properties");
    oc_jsonw_obj_begin(w);
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
}

static void fill_tools_list(oc_jsonw *w, void *ctx) {
    (void)ctx;
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "tools");
    oc_jsonw_arr_begin(w);
    {
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "name");
        oc_jsonw_str(w, "omnicheck_scan");
        oc_jsonw_key(w, "description");
        oc_jsonw_str(w, "Run the deterministic OmniCheck scan and return the full JSON report "
                        "(omnicheck.report/v0, byte-identical to `oc check` for the same inputs)");
        oc_jsonw_key(w, "inputSchema");
        write_scan_schema(w);
        oc_jsonw_obj_end(w);
    }
    {
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "name");
        oc_jsonw_str(w, "omnicheck_findings");
        oc_jsonw_key(w, "description");
        oc_jsonw_str(w, "Query findings (deterministic canonical order) with optional rule, "
                        "file-substring and severity filters, capped by limit");
        oc_jsonw_key(w, "inputSchema");
        write_findings_schema(w);
        oc_jsonw_obj_end(w);
    }
    {
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "name");
        oc_jsonw_str(w, "omnicheck_rules");
        oc_jsonw_key(w, "description");
        oc_jsonw_str(w, "List the built-in rule registry (ids, names, severities, descriptions)");
        oc_jsonw_key(w, "inputSchema");
        write_rules_schema(w);
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_arr_end(w);
    oc_jsonw_obj_end(w);
}

/* ------------------------------------------------------------ config */

static int build_call_config(const oc_config *base, const oc_json *args, oc_config *out) {
    oc_config_init(out);
    for (size_t i = 0; base && i < base->n; i++) {
        if (!oc_config_set(out, base->keys[i], base->vals[i]))
            return 0;
    }
    const oc_json *cfg = args ? oc_json_get(args, "config") : NULL;
    if (cfg && cfg->type == OC_JSON_OBJ) {
        for (size_t i = 0; i < cfg->n_fields; i++) {
            const oc_json *v = cfg->vals[i];
            if (!v || v->type != OC_JSON_STR)
                return 0;
            if (!oc_config_set(out, cfg->keys[i], v->str))
                return 0;
        }
    }
    return 1;
}

static int scan_options_from_args(const oc_mcp_options *opt, const oc_json *args, oc_arena *a,
                                  oc_scan_options *sopt, oc_config *call_cfg) {
    memset(sopt, 0, sizeof(*sopt));
    if (!build_call_config(opt->cfg, args, call_cfg))
        return 0;
    sopt->packs_dir = opt->packs_dir;
    sopt->cfg = call_cfg;

    const oc_json *paths = args ? oc_json_get(args, "paths") : NULL;
    if (paths && paths->type == OC_JSON_ARR) {
        const char **vec = (const char **)oc_arena_alloc(
            a, (oc_json_size(paths) ? oc_json_size(paths) : 1) * sizeof(char *));
        if (!vec)
            return 0;
        for (size_t i = 0; i < oc_json_size(paths); i++) {
            const char *p = oc_json_as_str(paths->items[i], NULL);
            if (!p)
                return 0;
            vec[i] = p;
        }
        sopt->paths = vec;
        sopt->n_paths = oc_json_size(paths);
    }
    if (!sopt->paths || sopt->n_paths == 0) {
        static const char *const cwd[1] = {"."};
        sopt->paths = cwd;
        sopt->n_paths = 1;
    }

    const oc_json *cov = args ? oc_json_get(args, "coverage") : NULL;
    if (cov && cov->type == OC_JSON_ARR) {
        const char **vec = (const char **)oc_arena_alloc(
            a, (oc_json_size(cov) ? oc_json_size(cov) : 1) * sizeof(char *));
        if (!vec)
            return 0;
        for (size_t i = 0; i < oc_json_size(cov); i++) {
            const char *p = oc_json_as_str(cov->items[i], NULL);
            if (!p)
                return 0;
            vec[i] = p;
        }
        sopt->coverage = vec;
        sopt->n_coverage = oc_json_size(cov);
    }

    const oc_json *triage = args ? oc_json_get(args, "triage") : NULL;
    if (triage && triage->type == OC_JSON_BOOL)
        sopt->want_triage = triage->boolean;
    return 1;
}

static void fill_content_error_text(oc_jsonw *w, void *ctx) {
    const char *text = (const char *)ctx;
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "content");
    oc_jsonw_arr_begin(w);
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "type");
    oc_jsonw_str(w, "text");
    oc_jsonw_key(w, "text");
    oc_jsonw_str(w, text);
    oc_jsonw_obj_end(w);
    oc_jsonw_arr_end(w);
    oc_jsonw_key(w, "isError");
    oc_jsonw_bool(w, 1);
    oc_jsonw_obj_end(w);
}

/* Shared scan execution for the two analysis tools. Returns OC_OK and a
 * populated res on success; on failure the caller gets an error result. */
static oc_code run_scan_locked(mcp_state *ms, const oc_json *args, oc_arena *a,
                               oc_scan_options *sopt, oc_config *call_cfg, oc_scan_result *res,
                               oc_status *st) {
    if (!scan_options_from_args(ms->opt, args, a, sopt, call_cfg)) {
        oc_status_set(st, OC_ERR_USAGE, "config values must be strings");
        return OC_ERR_USAGE;
    }
    memset(res, 0, sizeof(*res));
    return oc_scan_run(sopt, res, st);
}

static void write_finding_row(oc_jsonw *w, const oc_finding *f) {
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "rule");
    oc_jsonw_str(w, f->rule_id);
    oc_jsonw_key(w, "name");
    oc_jsonw_str(w, f->rule_name);
    oc_jsonw_key(w, "severity");
    oc_jsonw_str(w, oc_severity_str(f->severity));
    oc_jsonw_key(w, "file");
    oc_jsonw_str(w, f->file);
    oc_jsonw_key(w, "line");
    oc_jsonw_uint(w, f->line);
    oc_jsonw_key(w, "end_line");
    oc_jsonw_uint(w, f->end_line);
    oc_jsonw_key(w, "symbol");
    oc_jsonw_str(w, f->symbol);
    oc_jsonw_key(w, "message");
    oc_jsonw_str(w, f->message);
    oc_jsonw_key(w, "fingerprint");
    oc_jsonw_str(w, f->fingerprint);
    oc_jsonw_obj_end(w);
}

static void tool_scan(mcp_state *ms, const oc_json *args, const oc_json *id) {
    oc_arena a;
    oc_arena_init(&a);
    oc_scan_options sopt;
    oc_config call_cfg;
    oc_scan_result res;
    oc_status st;
    oc_status_clear(&st);

    oc_code rc = run_scan_locked(ms, args, &a, &sopt, &call_cfg, &res, &st);
    if (rc != OC_OK) {
        char msg[320];
        snprintf(msg, sizeof(msg), "Error: scan failed: %s", st.msg[0] ? st.msg : oc_code_str(rc));
        if (rc == OC_ERR_USAGE)
            send_error(ms->out, id, -32602, msg);
        else
            send_json(ms->out, id, "result", fill_content_error_text, (void *)msg);
        oc_config_destroy(&call_cfg);
        oc_arena_destroy(&a);
        return;
    }

    oc_jsonw report;
    oc_jsonw_init(&report);
    oc_report_write_json(&report, &res);
    send_json(ms->out, id, "result", fill_content, report.buf.data ? report.buf.data : "{}");
    oc_jsonw_destroy(&report);
    oc_scan_result_destroy(&res);
    oc_config_destroy(&call_cfg);
    oc_arena_destroy(&a);
}

static void tool_findings(mcp_state *ms, const oc_json *args, const oc_json *id) {
    const char *rule = args ? oc_json_as_str(oc_json_get(args, "rule"), NULL) : NULL;
    const char *file_contains =
        args ? oc_json_as_str(oc_json_get(args, "file_contains"), NULL) : NULL;
    const char *severity = args ? oc_json_as_str(oc_json_get(args, "severity"), NULL) : NULL;
    long limit = -1;
    const oc_json *limit_v = args ? oc_json_get(args, "limit") : NULL;
    if (limit_v && limit_v->type == OC_JSON_NUM)
        limit = (long)limit_v->number;
    if (limit < 0)
        limit = OC_MCP_DEFAULT_LIMIT;

    oc_arena a;
    oc_arena_init(&a);
    oc_scan_options sopt;
    oc_config call_cfg;
    oc_scan_result res;
    oc_status st;
    oc_status_clear(&st);

    oc_code rc = run_scan_locked(ms, args, &a, &sopt, &call_cfg, &res, &st);
    if (rc != OC_OK) {
        char msg[320];
        snprintf(msg, sizeof(msg), "Error: scan failed: %s", st.msg[0] ? st.msg : oc_code_str(rc));
        if (rc == OC_ERR_USAGE)
            send_error(ms->out, id, -32602, msg);
        else
            send_json(ms->out, id, "result", fill_content_error_text, (void *)msg);
        oc_config_destroy(&call_cfg);
        oc_arena_destroy(&a);
        return;
    }

    /* Deterministic filter over the canonically sorted findings. */
    oc_jsonw out;
    oc_jsonw_init(&out);
    oc_jsonw_obj_begin(&out);
    oc_jsonw_key(&out, "total");
    oc_jsonw_uint(&out, res.findings.n);
    oc_jsonw_key(&out, "count");
    size_t count = 0;
    size_t *idx = NULL;
    size_t n_idx = 0;
    if (res.findings.n > 0)
        idx = (size_t *)malloc(res.findings.n * sizeof(size_t));
    for (size_t i = 0; idx && i < res.findings.n; i++) {
        const oc_finding *f = &res.findings.items[i];
        if (rule && strcmp(f->rule_id, rule) != 0)
            continue;
        if (file_contains && !strstr(f->file, file_contains))
            continue;
        if (severity) {
            oc_severity want;
            if (!oc_severity_from_str(severity, &want) || f->severity != want)
                continue;
        }
        idx[n_idx++] = i;
    }
    for (size_t i = 0; idx && i < n_idx && i < (size_t)limit; i++)
        count++;
    oc_jsonw_uint(&out, count);
    oc_jsonw_key(&out, "findings");
    oc_jsonw_arr_begin(&out);
    for (size_t i = 0; idx && i < n_idx && i < (size_t)limit; i++)
        write_finding_row(&out, &res.findings.items[idx[i]]);
    oc_jsonw_arr_end(&out);
    oc_jsonw_obj_end(&out);
    free(idx);

    send_json(ms->out, id, "result", fill_content, out.buf.data ? out.buf.data : "{}");
    oc_jsonw_destroy(&out);
    oc_scan_result_destroy(&res);
    oc_config_destroy(&call_cfg);
    oc_arena_destroy(&a);
}

static void tool_rules(mcp_state *ms, const oc_json *args, const oc_json *id) {
    (void)args;
    oc_jsonw out;
    oc_jsonw_init(&out);
    oc_jsonw_obj_begin(&out);
    oc_jsonw_key(&out, "rules");
    oc_jsonw_arr_begin(&out);
    size_t n = 0;
    const oc_pass *const *all = oc_pass_all(&n);
    for (size_t i = 0; i < n; i++) {
        oc_jsonw_obj_begin(&out);
        oc_jsonw_key(&out, "id");
        oc_jsonw_str(&out, all[i]->id);
        oc_jsonw_key(&out, "name");
        oc_jsonw_str(&out, all[i]->name);
        oc_jsonw_key(&out, "default_severity");
        oc_jsonw_str(&out, oc_severity_str(all[i]->default_severity));
        oc_jsonw_key(&out, "description");
        oc_jsonw_str(&out, all[i]->description);
        oc_jsonw_obj_end(&out);
    }
    oc_jsonw_arr_end(&out);
    oc_jsonw_key(&out, "count");
    oc_jsonw_uint(&out, n);
    oc_jsonw_obj_end(&out);
    send_json(ms->out, id, "result", fill_content, out.buf.data ? out.buf.data : "{}");
    oc_jsonw_destroy(&out);
}

int oc_mcp_run(const oc_mcp_options *opt, FILE *in, FILE *out, oc_status *st) {
    (void)st; /* the session is fail-open: per-message errors are answered
               * as JSON-RPC errors and the loop never aborts */
    mcp_state ms;
    memset(&ms, 0, sizeof(ms));
    ms.out = out;
    ms.opt = opt;

    char *line = NULL;
    size_t cap = 0;
    ssize_t got;
    while ((got = getline(&line, &cap, in)) >= 0) {
        size_t len = (size_t)got;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;
        if (len == 0)
            continue; /* blank line: MCP frames are one JSON per line */

        oc_arena a;
        oc_arena_init(&a);
        oc_status pst;
        oc_status_clear(&pst);
        oc_json *msg = oc_json_parse(&a, line, len, &pst);
        if (!msg) {
            send_error(out, NULL, -32700, "Parse error: invalid JSON message");
            oc_arena_destroy(&a);
            continue;
        }
        const char *method = oc_json_as_str(oc_json_get(msg, "method"), NULL);
        const oc_json *id = oc_json_get(msg, "id");
        const oc_json *params = oc_json_get(msg, "params");
        int is_request = id != NULL && id->type != OC_JSON_NULL;

        if (!method) {
            if (is_request)
                send_error(out, id, -32600, "Invalid Request: missing method");
            oc_arena_destroy(&a);
            continue;
        }

        if (strncmp(method, "notifications/", 14) == 0) {
            oc_arena_destroy(&a);
            continue;
        }
        if (strcmp(method, "initialize") == 0) {
            const char *cv = oc_json_as_str(oc_json_get(params, "protocolVersion"), NULL);
            send_json(out, id, "result", fill_initialize, (void *)cv);
        } else if (strcmp(method, "ping") == 0) {
            send_json(out, id, "result", fill_empty, NULL);
        } else if (strcmp(method, "tools/list") == 0) {
            send_json(out, id, "result", fill_tools_list, NULL);
        } else if (strcmp(method, "tools/call") == 0) {
            const char *name = oc_json_as_str(oc_json_get(params, "name"), NULL);
            const oc_json *args = oc_json_get(params, "arguments");
            if (!name) {
                send_error(out, id, -32602, "Invalid params: tools/call requires a name");
            } else if (strcmp(name, "omnicheck_scan") == 0) {
                tool_scan(&ms, args, id);
            } else if (strcmp(name, "omnicheck_findings") == 0) {
                tool_findings(&ms, args, id);
            } else if (strcmp(name, "omnicheck_rules") == 0) {
                tool_rules(&ms, args, id);
            } else {
                char msgbuf[128];
                snprintf(msgbuf, sizeof(msgbuf), "Unknown tool: %s", name);
                send_error(out, id, -32602, msgbuf);
            }
        } else if (is_request) {
            char msgbuf[160];
            snprintf(msgbuf, sizeof(msgbuf), "Method not found: %s", method);
            send_error(out, id, -32601, msgbuf);
        }
        oc_arena_destroy(&a);
    }
    free(line);
    return 0;
}
