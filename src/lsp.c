#include "oc/lsp.h"

#include <stdlib.h>
#include <string.h>

#include "oc/arena.h"
#include "oc/finding.h"
#include "oc/graph.h"
#include "oc/json.h"
#include "oc/scan.h"
#include "oc/str.h"

/*
 * LSP 3.17 adapter over the shared scan pipeline (see oc/lsp.h). The
 * protocol surface is deliberately small: initialize / initialized /
 * shutdown / exit, textDocument/didOpen|didChange|didSave publishing
 * diagnostics, and textDocument/documentSymbol. Everything deterministic
 * comes from oc_scan_run; this file only converts shapes.
 */

#define OC_LSP_MAX_HEADERS 32

/* ------------------------------------------------------------- framing */

static int read_header_line(FILE *in, char *buf, size_t cap) {
    size_t n = 0;
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        if (n + 1 < cap)
            buf[n++] = (char)c;
        else
            return -1; /* header line too long */
    }
    buf[n] = '\0';
    if (c == EOF && n == 0)
        return 0; /* clean EOF between messages */
    return 1;
}

oc_code oc_lsp_read_message(FILE *in, char **out_body, size_t *out_len, oc_arena *a,
                            oc_status *st) {
    long content_length = -1;
    char line[256];
    int got_any_header = 0;
    for (int i = 0; i < OC_LSP_MAX_HEADERS; i++) {
        int rc = read_header_line(in, line, sizeof(line));
        if (rc == 0)
            break;
        if (rc < 0) {
            oc_status_set(st, OC_ERR_IO, "LSP header line too long");
            return OC_ERR_IO;
        }
        if (line[0] == '\0')
            break; /* end of headers */
        got_any_header = 1;
        /* Case-insensitive Content-Length. */
        char name[64], value[192];
        const char *colon = strchr(line, ':');
        if (!colon)
            continue;
        size_t nlen = (size_t)(colon - line);
        if (nlen >= sizeof(name))
            continue;
        memcpy(name, line, nlen);
        name[nlen] = '\0';
        snprintf(value, sizeof(value), "%s", colon + 1);
        char *p = value;
        while (*p == ' ' || *p == '\t')
            p++;
        for (char *q = name; *q; q++) {
            if (*q >= 'A' && *q <= 'Z')
                *q = (char)(*q - 'A' + 'a');
        }
        if (strcmp(name, "content-length") == 0)
            content_length = atol(p);
    }
    (void)got_any_header;
    if (content_length < 0) {
        *out_body = NULL;
        *out_len = 0;
        return OC_OK; /* EOF between messages */
    }
    if (content_length > 64u * 1024u * 1024u) {
        oc_status_set(st, OC_ERR_IO, "LSP message body too large (%ld bytes)", content_length);
        return OC_ERR_IO;
    }
    char *body = (char *)oc_arena_alloc(a, (size_t)content_length + 1);
    if (!body) {
        oc_status_set(st, OC_ERR_ALLOC, "out of memory reading LSP message");
        return OC_ERR_ALLOC;
    }
    size_t got = fread(body, 1, (size_t)content_length, in);
    if (got != (size_t)content_length) {
        oc_status_set(st, OC_ERR_IO, "LSP body truncated (%zu/%ld bytes)", got, content_length);
        return OC_ERR_IO;
    }
    body[content_length] = '\0';
    *out_body = body;
    *out_len = (size_t)content_length;
    return OC_OK;
}

oc_code oc_lsp_write_message(FILE *out, const char *body, size_t len) {
    if (fprintf(out, "Content-Length: %zu\n\n", len) < 0)
        return OC_ERR_IO;
    if (fwrite(body, 1, len, out) != len)
        return OC_ERR_IO;
    if (fflush(out) != 0)
        return OC_ERR_IO;
    return OC_OK;
}

/* ------------------------------------------------------------- URIs */

static int hex_val(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

char *oc_lsp_uri_from_path(oc_arena *a, const char *path) {
    /* Percent-encode everything outside the unreserved/path-safe set. */
    oc_strbuf b;
    oc_strbuf_init(&b);
    oc_strbuf_appends(&b, "file://");
    for (const char *p = path; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '/' || c == '.' || c == '_' || c == '-' || c == '~') {
            char one[2] = {(char)c, 0};
            oc_strbuf_appends(&b, one);
        } else {
            char esc[4];
            snprintf(esc, sizeof(esc), "%%%02X", c);
            oc_strbuf_appends(&b, esc);
        }
    }
    char *out = oc_arena_strdup(a, b.data ? b.data : "");
    oc_strbuf_destroy(&b);
    return out;
}

char *oc_lsp_path_from_uri(oc_arena *a, const char *uri) {
    if (strncmp(uri, "file://", 7) != 0)
        return NULL;
    const char *p = uri + 7;
    /* file://localhost/... -> drop an authority of exactly "localhost". */
    if (strncmp(p, "localhost/", 10) == 0)
        p += 9; /* keep the leading slash */
    oc_strbuf b;
    oc_strbuf_init(&b);
    while (*p) {
        if (*p == '%' && hex_val((unsigned char)p[1]) >= 0 && hex_val((unsigned char)p[2]) >= 0) {
            char byte = (char)((hex_val((unsigned char)p[1]) << 4) | hex_val((unsigned char)p[2]));
            char one[2] = {byte, 0};
            oc_strbuf_appends(&b, one);
            p += 3;
        } else {
            char one[2] = {*p, 0};
            oc_strbuf_appends(&b, one);
            p++;
        }
    }
    char *out = oc_arena_strdup(a, b.data ? b.data : "");
    oc_strbuf_destroy(&b);
    return out;
}

/* ------------------------------------------------------- JSON helpers */

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
    case OC_JSON_NULL:
        oc_jsonw_null(w);
        break;
    default:
        oc_jsonw_null(w);
        break;
    }
}

static void send_result_null(FILE *out, const oc_json *id) {
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "jsonrpc");
    oc_jsonw_str(&w, "2.0");
    oc_jsonw_key(&w, "id");
    write_json_id(&w, id);
    oc_jsonw_key(&w, "result");
    oc_jsonw_null(&w);
    oc_jsonw_obj_end(&w);
    if (w.buf.data)
        oc_lsp_write_message(out, w.buf.data, w.buf.len);
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
    if (w.buf.data)
        oc_lsp_write_message(out, w.buf.data, w.buf.len);
    oc_jsonw_destroy(&w);
}

/* ------------------------------------------------------ diagnostics */

static int lsp_severity_for(oc_severity s) {
    switch (s) {
    case OC_SEV_ERROR:
        return 1;
    case OC_SEV_WARN:
        return 2;
    default:
        return 3; /* Information (OC00x info findings) */
    }
}

static void write_range(oc_jsonw *w, size_t start_line, size_t start_col, size_t end_line,
                        size_t end_col) {
    oc_jsonw_key(w, "range");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "start");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "line");
    oc_jsonw_uint(w, start_line ? start_line - 1 : 0);
    oc_jsonw_key(w, "character");
    oc_jsonw_uint(w, start_col ? start_col - 1 : 0);
    oc_jsonw_obj_end(w);
    oc_jsonw_key(w, "end");
    oc_jsonw_obj_begin(w);
    oc_jsonw_key(w, "line");
    oc_jsonw_uint(w, end_line ? end_line - 1 : 0);
    oc_jsonw_key(w, "character");
    oc_jsonw_uint(w, end_col ? end_col - 1 : 0);
    oc_jsonw_obj_end(w);
    oc_jsonw_obj_end(w);
}

/* One publishDiagnostics notification for `uri`, holding every finding
 * of the scan for that file (canonical order) plus named parse errors. */
static oc_code publish_diagnostics(FILE *out, oc_arena *a, const char *uri,
                                   const oc_scan_result *res) {
    char *path = oc_lsp_path_from_uri(a, uri);
    if (!path)
        return OC_OK; /* non-file URI: nothing to match against */

    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "jsonrpc");
    oc_jsonw_str(&w, "2.0");
    oc_jsonw_key(&w, "method");
    oc_jsonw_str(&w, "textDocument/publishDiagnostics");
    oc_jsonw_key(&w, "params");
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "uri");
    oc_jsonw_str(&w, uri);
    oc_jsonw_key(&w, "diagnostics");
    oc_jsonw_arr_begin(&w);

    for (size_t i = 0; i < res->findings.n; i++) {
        const oc_finding *f = &res->findings.items[i];
        if (strcmp(f->file, path) != 0)
            continue;
        oc_jsonw_obj_begin(&w);
        write_range(&w, f->line, f->col, f->end_line > f->line ? f->end_line : f->line,
                    f->end_line > f->line ? 1 : (f->col ? f->col + 1 : 1));
        oc_jsonw_key(&w, "severity");
        oc_jsonw_int(&w, lsp_severity_for(f->severity));
        oc_jsonw_key(&w, "code");
        oc_jsonw_str(&w, f->rule_id);
        oc_jsonw_key(&w, "source");
        oc_jsonw_str(&w, "omnicheck");
        oc_jsonw_key(&w, "message");
        oc_jsonw_str(&w, f->message);
        oc_jsonw_obj_end(&w);
    }
    for (size_t i = 0; i < res->n_parse_errors; i++) {
        const oc_scan_parse_error *pe = &res->parse_errors[i];
        if (strcmp(pe->file, path) != 0)
            continue;
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "file did not parse cleanly: %zu error node(s), first at line %zu column %zu; "
                 "analysis covers only the parseable portion",
                 pe->error_nodes, pe->first_line, pe->first_column);
        oc_jsonw_obj_begin(&w);
        write_range(&w, pe->first_line ? pe->first_line : 1,
                    pe->first_column ? pe->first_column : 1, pe->first_line ? pe->first_line : 1,
                    pe->first_column ? pe->first_column + 1 : 2);
        oc_jsonw_key(&w, "severity");
        oc_jsonw_int(&w, 1); /* Error: partial analysis is an honesty failure */
        oc_jsonw_key(&w, "code");
        oc_jsonw_str(&w, "parse-error");
        oc_jsonw_key(&w, "source");
        oc_jsonw_str(&w, "omnicheck");
        oc_jsonw_key(&w, "message");
        oc_jsonw_str(&w, msg);
        oc_jsonw_obj_end(&w);
    }

    oc_jsonw_arr_end(&w);
    oc_jsonw_obj_end(&w);
    oc_jsonw_obj_end(&w);
    oc_code rc = OC_OK;
    if (w.buf.data)
        rc = oc_lsp_write_message(out, w.buf.data, w.buf.len);
    oc_jsonw_destroy(&w);
    return rc;
}

/* ---------------------------------------------- document symbols */

static int symbol_kind_for(oc_node_kind k) {
    switch (k) {
    case OC_NODE_CLASS:
        return 5; /* Class */
    case OC_NODE_VARIABLE:
        return 13; /* Variable */
    default:
        return 12; /* Function */
    }
}

/* textDocument/documentSymbol: flat DocumentSymbol[] from the graph. */
static oc_code write_document_symbols(oc_jsonw *w, const oc_scan_result *res, const char *path) {
    oc_jsonw_arr_begin(w);
    for (size_t i = 0; i < res->graph.n_nodes; i++) {
        const oc_node *n = &res->graph.nodes[i];
        if (n->kind == OC_NODE_FILE)
            continue;
        if (strcmp(n->path, path) != 0)
            continue;
        size_t start_line = n->def_start_line ? n->def_start_line : n->decl_line;
        size_t end_line = n->def_end_line ? n->def_end_line : n->decl_line;
        if (end_line < start_line)
            end_line = start_line;
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "name");
        oc_jsonw_str(w, n->name);
        oc_jsonw_key(w, "kind");
        oc_jsonw_int(w, symbol_kind_for(n->kind));
        write_range(w, start_line, 1, end_line + 1, 1);
        oc_jsonw_key(w, "selectionRange");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "start");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "line");
        oc_jsonw_uint(w, n->decl_line ? n->decl_line - 1 : 0);
        oc_jsonw_key(w, "character");
        oc_jsonw_uint(w, 0);
        oc_jsonw_obj_end(w);
        oc_jsonw_key(w, "end");
        oc_jsonw_obj_begin(w);
        oc_jsonw_key(w, "line");
        oc_jsonw_uint(w, n->decl_line ? n->decl_line - 1 : 0);
        oc_jsonw_key(w, "character");
        oc_jsonw_uint(w, 1);
        oc_jsonw_obj_end(w);
        oc_jsonw_obj_end(w);
        oc_jsonw_obj_end(w);
    }
    oc_jsonw_arr_end(w);
    return OC_OK;
}

/* ----------------------------------------------------------- the loop */

typedef struct lsp_state {
    FILE *in;
    FILE *out;
    const oc_lsp_options *opt;
    char root_buf[1024];   /* workspace root, owned here: per-message
                            * arenas die between messages, so the root
                            * can never point into one */
    const char *root_path; /* points at root_buf or "." */
    int shutdown_seen;
} lsp_state;

static oc_code scan_and_publish(lsp_state *ls, oc_arena *a, const char *uri) {
    oc_scan_options sopt;
    memset(&sopt, 0, sizeof(sopt));
    sopt.packs_dir = ls->opt->packs_dir;
    sopt.cfg = ls->opt->cfg;
    const char *roots[1] = {ls->root_path ? ls->root_path : "."};
    sopt.paths = roots;
    sopt.n_paths = 1;

    oc_scan_result res;
    memset(&res, 0, sizeof(res));
    oc_status st;
    oc_status_clear(&st);
    oc_code rc = oc_scan_run(&sopt, &res, &st);
    if (rc != OC_OK) {
        /* Fail open: one failed scan must not kill the session. The
         * client keeps its last diagnostics; the error is logged. */
        fprintf(stderr, "omnicheck lsp: scan failed: %s\n", st.msg[0] ? st.msg : oc_code_str(rc));
        return OC_OK;
    }
    rc = publish_diagnostics(ls->out, a, uri, &res);
    oc_scan_result_destroy(&res);
    return rc;
}

static int handle_message(lsp_state *ls, oc_arena *a, const char *body, size_t len) {
    oc_status st;
    oc_status_clear(&st);
    oc_json *msg = oc_json_parse(a, body, len, &st);
    if (!msg) {
        send_error(ls->out, NULL, -32700, "Parse error: invalid JSON message");
        return 0;
    }
    const char *method = oc_json_as_str(oc_json_get(msg, "method"), NULL);
    const oc_json *id = oc_json_get(msg, "id");
    const oc_json *params = oc_json_get(msg, "params");

    if (!method) {
        /* Responses from the client are ignored; garbage gets an error
         * only when an id is present. */
        if (id)
            send_error(ls->out, id, -32600, "Invalid Request: missing method");
        return 0;
    }

    if (strcmp(method, "initialize") == 0) {
        const char *root_uri = oc_json_as_str(oc_json_get(params, "rootUri"), NULL);
        if (!root_uri) {
            const oc_json *folders = oc_json_get(params, "workspaceFolders");
            if (folders && oc_json_size(folders) > 0)
                root_uri = oc_json_as_str(oc_json_get(folders->items[0], "uri"), NULL);
        }
        ls->root_path = ".";
        if (root_uri) {
            char *p = oc_lsp_path_from_uri(a, root_uri);
            if (p && p[0]) {
                snprintf(ls->root_buf, sizeof(ls->root_buf), "%s", p);
                ls->root_path = ls->root_buf;
            }
        }
        /* Full response in one writer (jsonrpc, id, result). */
        oc_jsonw w;
        oc_jsonw_init(&w);
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "jsonrpc");
        oc_jsonw_str(&w, "2.0");
        oc_jsonw_key(&w, "id");
        write_json_id(&w, id);
        oc_jsonw_key(&w, "result");
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "capabilities");
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "textDocumentSync");
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "openClose");
        oc_jsonw_bool(&w, 1);
        oc_jsonw_key(&w, "change");
        oc_jsonw_int(&w, 1); /* Full */
        oc_jsonw_obj_end(&w);
        oc_jsonw_key(&w, "documentSymbolProvider");
        oc_jsonw_bool(&w, 1);
        oc_jsonw_obj_end(&w);
        oc_jsonw_key(&w, "serverInfo");
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "name");
        oc_jsonw_str(&w, "omnicheck");
        oc_jsonw_key(&w, "version");
        oc_jsonw_str(&w, OC_VERSION_STRING);
        oc_jsonw_obj_end(&w);
        oc_jsonw_obj_end(&w); /* result */
        oc_jsonw_obj_end(&w); /* outer */
        if (w.buf.data)
            oc_lsp_write_message(ls->out, w.buf.data, w.buf.len);
        oc_jsonw_destroy(&w);
        return 0;
    }
    if (strcmp(method, "initialized") == 0)
        return 0;
    if (strcmp(method, "shutdown") == 0) {
        ls->shutdown_seen = 1;
        send_result_null(ls->out, id);
        return 0;
    }
    if (strcmp(method, "exit") == 0)
        return ls->shutdown_seen ? 1 : 2;

    if (strcmp(method, "textDocument/didOpen") == 0 ||
        strcmp(method, "textDocument/didChange") == 0 ||
        strcmp(method, "textDocument/didSave") == 0) {
        /* v0 contract: all three triggers re-scan the DISK state; the
         * didChange buffer text is deliberately not analyzed (see
         * oc/lsp.h). A fresh scan per event keeps diagnostics exact. */
        const char *uri =
            oc_json_as_str(oc_json_get(oc_json_get(params, "textDocument"), "uri"), NULL);
        if (uri) {
            if (scan_and_publish(ls, a, uri) != OC_OK)
                return 3; /* fatal IO writing to the client */
        }
        return 0;
    }
    if (strcmp(method, "textDocument/documentSymbol") == 0) {
        const char *uri =
            oc_json_as_str(oc_json_get(oc_json_get(params, "textDocument"), "uri"), NULL);
        if (!uri) {
            send_error(ls->out, id, -32602, "Invalid params: textDocument.uri required");
            return 0;
        }
        char *path = oc_lsp_path_from_uri(a, uri);
        if (!path) {
            send_error(ls->out, id, -32602, "Invalid params: only file:// URIs are supported");
            return 0;
        }
        oc_scan_options sopt;
        memset(&sopt, 0, sizeof(sopt));
        sopt.packs_dir = ls->opt->packs_dir;
        sopt.cfg = ls->opt->cfg;
        const char *roots[1] = {ls->root_path ? ls->root_path : "."};
        sopt.paths = roots;
        sopt.n_paths = 1;
        oc_scan_result res;
        memset(&res, 0, sizeof(res));
        oc_status sst;
        oc_status_clear(&sst);
        oc_code rc = oc_scan_run(&sopt, &res, &sst);
        if (rc != OC_OK) {
            send_error(ls->out, id, -32603,
                       sst.msg[0] ? sst.msg : "internal error during analysis");
            return 0;
        }
        /* Full response in one writer: result carries the symbol array. */
        oc_jsonw w;
        oc_jsonw_init(&w);
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "jsonrpc");
        oc_jsonw_str(&w, "2.0");
        oc_jsonw_key(&w, "id");
        write_json_id(&w, id);
        oc_jsonw_key(&w, "result");
        write_document_symbols(&w, &res, path);
        oc_jsonw_obj_end(&w);
        if (w.buf.data)
            oc_lsp_write_message(ls->out, w.buf.data, w.buf.len);
        oc_jsonw_destroy(&w);
        oc_scan_result_destroy(&res);
        return 0;
    }

    /* Unknown: requests get MethodNotFound; notifications are ignored. */
    if (id) {
        char msgbuf[160];
        snprintf(msgbuf, sizeof(msgbuf), "Method not found: %s", method);
        send_error(ls->out, id, -32601, msgbuf);
    }
    return 0;
}

int oc_lsp_run(const oc_lsp_options *opt, FILE *in, FILE *out, oc_status *st) {
    lsp_state ls;
    memset(&ls, 0, sizeof(ls));
    ls.in = in;
    ls.out = out;
    ls.opt = opt;
    ls.root_buf[0] = '\0';
    ls.root_path = ".";
    ls.shutdown_seen = 0;

    int exit_code = 0;
    for (;;) {
        oc_arena a;
        oc_arena_init(&a);
        char *body = NULL;
        size_t len = 0;
        oc_code rc = oc_lsp_read_message(in, &body, &len, &a, st);
        if (rc != OC_OK) {
            exit_code = 2;
            oc_arena_destroy(&a);
            break;
        }
        if (!body) {
            /* EOF: treat as a client death without `exit`. */
            oc_arena_destroy(&a);
            exit_code = ls.shutdown_seen ? 0 : 0; /* stdin EOF is a clean stop for tests */
            break;
        }
        int hrc = handle_message(&ls, &a, body, len);
        oc_arena_destroy(&a);
        if (hrc == 1) {
            exit_code = 0;
            break;
        }
        if (hrc == 2) {
            exit_code = 1;
            break;
        }
        if (hrc == 3) {
            oc_status_set(st, OC_ERR_IO, "failed writing to the LSP client");
            exit_code = 2;
            break;
        }
    }
    return exit_code;
}
