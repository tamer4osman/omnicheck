/* Sprint 4 (WBS 3.3): LSP server tests. Framing + URI helpers are unit
 * tested in-process; a full scripted session runs the real `oc lsp`
 * binary over pipes (initialize -> didOpen -> diagnostics -> document
 * symbols -> unknown method -> shutdown -> exit). */
#include "oc/arena.h"
#include "oc/json.h"
#include "oc/lsp.h"
#include "testkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef OC_OC_BIN
#error "OC_OC_BIN must point at the built oc binary"
#endif
#ifndef OC_REPO_ROOT
#error "OC_REPO_ROOT must point at the repository root"
#endif

/* --- framing + URI units ---------------------------------------------- */
static void unit_framing(void) {
    /* Read a hand-framed message (CRLF + LF tolerance). */
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (!f)
        return;
    const char *body1 = "{\"jsonrpc\":\"2.0\",\"id\":1}";
    fprintf(f, "Content-Length: %zu\n\n%s", strlen(body1), body1);
    fprintf(f, "Content-Length: %zu\n\n%s", strlen(body1), body1);
    rewind(f);

    oc_arena a;
    oc_arena_init(&a);
    char *got = NULL;
    size_t got_len = 0;
    oc_status st;
    oc_status_clear(&st);
    CHECK(oc_lsp_read_message(f, &got, &got_len, &a, &st) == OC_OK);
    CHECK(got != NULL);
    CHECK(got_len == strlen(body1));
    CHECK_STR_EQ(got, body1);
    /* Second message framed with bare LF headers also parses. */
    CHECK(oc_lsp_read_message(f, &got, &got_len, &a, &st) == OC_OK);
    CHECK(got != NULL);
    CHECK(got_len == strlen(body1));
    /* EOF between messages. */
    CHECK(oc_lsp_read_message(f, &got, &got_len, &a, &st) == OC_OK);
    CHECK(got == NULL);
    fclose(f);

    /* Write -> read roundtrip through the real framing. */
    f = tmpfile();
    CHECK(f != NULL);
    if (f) {
        const char *msg = "{\"x\":[1,2,3],\"s\":\"a\\\"b\"}";
        CHECK(oc_lsp_write_message(f, msg, strlen(msg)) == OC_OK);
        rewind(f);
        CHECK(oc_lsp_read_message(f, &got, &got_len, &a, &st) == OC_OK);
        CHECK(got != NULL);
        CHECK(got_len == strlen(msg));
        CHECK_STR_EQ(got, msg);
        fclose(f);
    }

    /* URI roundtrip: spaces percent-encode, then decode exactly. */
    char *uri = oc_lsp_uri_from_path(&a, "/tmp/dir with space/x.js");
    CHECK(uri != NULL);
    CHECK_STR_EQ(uri, "file:///tmp/dir%20with%20space/x.js");
    char *back = oc_lsp_path_from_uri(&a, uri);
    CHECK(back != NULL);
    CHECK_STR_EQ(back, "/tmp/dir with space/x.js");
    /* Non-file URIs are rejected, not guessed. */
    CHECK(oc_lsp_path_from_uri(&a, "https://example.com/x") == NULL);

    oc_arena_destroy(&a);
}

/* --- full session against the real binary ------------------------------ */
typedef struct frame_vec {
    char **items;
    size_t n;
    size_t cap;
} frame_vec;

static void frames_push(frame_vec *v, const char *body) {
    if (v->n == v->cap) {
        size_t ncap = v->cap ? v->cap * 2 : 16;
        char **grown = (char **)realloc(v->items, ncap * sizeof(*grown));
        if (!grown)
            return;
        v->items = grown;
        v->cap = ncap;
    }
    v->items[v->n++] = strdup(body);
}

static void frames_free(frame_vec *v) {
    for (size_t i = 0; i < v->n; i++)
        free(v->items[i]);
    free(v->items);
    v->items = NULL;
    v->n = v->cap = 0;
}

/* Serializes one client message into `buf`. */
static size_t frame_into(char *buf, size_t cap, const char *json) {
    size_t len = strlen(json);
    return (size_t)snprintf(buf, cap, "Content-Length: %zu\n\n%s", len, json);
}

static void session_test(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char packs[512], fix[512];
    snprintf(packs, sizeof(packs), "%s/packs", root);
    snprintf(fix, sizeof(fix), "%s/tests/fixtures_sprint4/lspdemo", root);

    int in_pipe[2], out_pipe[2];
    CHECK(pipe(in_pipe) == 0);
    CHECK(pipe(out_pipe) == 0);
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        /* Child: serve the protocol over the pipes. */
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execl(bin, "oc", "lsp", "--packs-dir", packs, (char *)NULL);
        _exit(127); /* exec failed */
    }
    close(in_pipe[0]);
    close(out_pipe[1]);

    /* Scripted session: deterministic message order, so the reply stream
     * is deterministic too. */
    char buf[8192];
    size_t off = 0;
    char fix_uri[600];
    snprintf(fix_uri, sizeof(fix_uri), "file://%s", fix);

    { /* initialize with the workspace root */
        char init_full[1024];
        snprintf(init_full, sizeof(init_full),
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                 "\"params\":{\"rootUri\":\"%s\"}}",
                 fix_uri);
        off = frame_into(buf, sizeof(buf), init_full);
    }
    {
        const char *m = "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
        off += frame_into(buf + off, sizeof(buf) - off, m);
    }
    {
        char m[1024];
        snprintf(m, sizeof(m),
                 "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":"
                 "{\"textDocument\":{\"uri\":\"%s/demo.js\",\"text\":\"ignored\"}}}",
                 fix_uri);
        off += frame_into(buf + off, sizeof(buf) - off, m);
    }
    {
        char m[1024];
        snprintf(m, sizeof(m),
                 "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":"
                 "{\"textDocument\":{\"uri\":\"%s/broken.js\",\"text\":\"ignored\"}}}",
                 fix_uri);
        off += frame_into(buf + off, sizeof(buf) - off, m);
    }
    {
        char m[1024];
        snprintf(m, sizeof(m),
                 "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/documentSymbol\","
                 "\"params\":{\"textDocument\":{\"uri\":\"%s/demo.js\"}}}",
                 fix_uri);
        off += frame_into(buf + off, sizeof(buf) - off, m);
    }
    {
        const char *m = "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/unknownThing\"}";
        off += frame_into(buf + off, sizeof(buf) - off, m);
    }
    {
        const char *m = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"shutdown\"}";
        off += frame_into(buf + off, sizeof(buf) - off, m);
    }
    {
        const char *m = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";
        off += frame_into(buf + off, sizeof(buf) - off, m);
    }

    /* Send everything (fits the pipe buffer), then close stdin so the
     * session ends deterministically even if the server stalls. */
    size_t sent = 0;
    while (sent < off) {
        ssize_t w = write(in_pipe[1], buf + sent, off - sent);
        if (w <= 0)
            break;
        sent += (size_t)w;
    }
    close(in_pipe[1]);

    /* Read the whole reply stream to EOF. */
    frame_vec frames;
    memset(&frames, 0, sizeof(frames));
    FILE *rf = fdopen(out_pipe[0], "rb");
    CHECK(rf != NULL);
    if (rf) {
        oc_arena a;
        oc_arena_init(&a);
        for (;;) {
            char *body = NULL;
            size_t blen = 0;
            oc_status st;
            oc_status_clear(&st);
            oc_code rc = oc_lsp_read_message(rf, &body, &blen, &a, &st);
            if (rc != OC_OK || !body)
                break;
            frames_push(&frames, body);
        }
        oc_arena_destroy(&a);
        fclose(rf);
    }

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    CHECK(WIFEXITED(wstatus));
    CHECK(WEXITSTATUS(wstatus) == 0); /* clean shutdown -> exit 0 */
    CHECK(frames.n == 6);             /* init, diag demo, diag broken, symbols, err9, shutdown */

    /* Assertions over the parsed frames. */
    int saw_init_caps = 0, saw_diag_demo_oc002 = 0, saw_diag_broken_parse = 0;
    int saw_symbols_alpha = 0, saw_err9 = 0, saw_shutdown_null = 0;
    oc_arena pa;
    oc_arena_init(&pa);
    for (size_t i = 0; i < frames.n; i++) {
        oc_status st;
        oc_status_clear(&st);
        oc_json *m = oc_json_parse(&pa, frames.items[i], strlen(frames.items[i]), &st);
        if (!m)
            continue;
        const char *method = oc_json_as_str(oc_json_get(m, "method"), "");
        const oc_json *id = oc_json_get(m, "id");
        double idn = id ? oc_json_as_num(id, -1) : -1;
        if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
            const oc_json *params = oc_json_get(m, "params");
            const char *uri = oc_json_as_str(oc_json_get(params, "uri"), "");
            const oc_json *diags = oc_json_get(params, "diagnostics");
            for (size_t k = 0; diags && k < oc_json_size(diags); k++) {
                const oc_json *d = diags->items[k];
                const char *code = oc_json_as_str(oc_json_get(d, "code"), "");
                const oc_json *range = oc_json_get(d, "range");
                const oc_json *start = range ? oc_json_get(range, "start") : NULL;
                size_t line = (size_t)oc_json_as_num(oc_json_get(start, "line"), -1);
                if (strstr(uri, "demo.js") && strcmp(code, "OC002") == 0 && line == 0)
                    saw_diag_demo_oc002 = 1;
                if (strstr(uri, "broken.js") && strcmp(code, "parse-error") == 0 &&
                    oc_json_as_num(oc_json_get(d, "severity"), -1) == 1)
                    saw_diag_broken_parse = 1;
            }
        } else if (idn == 1) {
            const oc_json *result = oc_json_get(m, "result");
            const oc_json *caps = oc_json_get(result, "capabilities");
            if (oc_json_as_bool(oc_json_get(caps, "documentSymbolProvider"), 0) == 1)
                saw_init_caps = 1;
            CHECK_STR_EQ(
                oc_json_as_str(oc_json_get(oc_json_get(result, "serverInfo"), "name"), "?"),
                "omnicheck");
        } else if (idn == 2) {
            const oc_json *result = oc_json_get(m, "result");
            if (result && oc_json_size(result) >= 1) {
                const oc_json *s0 = result->items[0];
                if (strcmp(oc_json_as_str(oc_json_get(s0, "name"), ""), "alpha") == 0 &&
                    oc_json_as_num(oc_json_get(s0, "kind"), -1) == 12)
                    saw_symbols_alpha = 1;
            }
        } else if (idn == 9) {
            const oc_json *error = oc_json_get(m, "error");
            if (oc_json_as_num(oc_json_get(error, "code"), 0) == -32601)
                saw_err9 = 1;
        } else if (idn == 3) {
            const oc_json *result = oc_json_get(m, "result");
            if (result && result->type == OC_JSON_NULL)
                saw_shutdown_null = 1;
        }
    }
    oc_arena_destroy(&pa);
    CHECK(saw_init_caps);
    CHECK(saw_diag_demo_oc002);
    CHECK(saw_diag_broken_parse);
    CHECK(saw_symbols_alpha);
    CHECK(saw_err9);
    CHECK(saw_shutdown_null);

    frames_free(&frames);
}

int main(void) {
    unit_framing();
    session_test();
    TEST_SUMMARY();
}
