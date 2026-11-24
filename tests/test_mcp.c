/* Sprint 4 (WBS 3.4): MCP server tests. A full scripted session runs the
 * real `oc mcp` binary over pipes: initialize -> tools/list -> tools/call
 * (scan / findings / rules) -> protocol errors, then EOF. The session is
 * run twice and the transcript must be byte-identical. */
#include "oc/common.h"
#include "oc/json.h"
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

static const char *const SESSION_JSONL[] = {
    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
    "\"params\":{\"protocolVersion\":\"2025-03-26\",\"capabilities\":{},"
    "\"clientInfo\":{\"name\":\"test-agent\",\"version\":\"0\"}}}",
    "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
    "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}",
    "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":"
    "{\"name\":\"omnicheck_rules\",\"arguments\":{}}}",
    "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":"
    "{\"name\":\"omnicheck_scan\",\"arguments\":{}}}", /* default root = cwd */
    "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":"
    "{\"name\":\"omnicheck_findings\",\"arguments\":{}}}",
    "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":"
    "{\"name\":\"not_a_tool\",\"arguments\":{}}}",
    "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"resources/list\"}",
    NULL /* replaced per-run with paths fixed to the fixture root */
};
#define IDX_SCAN     4
#define IDX_FINDINGS 5

static char g_session[16][1024];
static const char *g_lines[16];

/* Builds the session lines with the fixture root baked into the two
 * analysis calls. Returns the number of lines. */
static size_t build_session(const char *fix) {
    size_t n = 0;
    for (size_t i = 0; i < sizeof(SESSION_JSONL) / sizeof(SESSION_JSONL[0]) - 1; i++) {
        if (i == IDX_SCAN) {
            snprintf(g_session[i], sizeof(g_session[i]),
                     "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":"
                     "{\"name\":\"omnicheck_scan\",\"arguments\":{\"paths\":[\"%s\"]}}}",
                     fix);
        } else if (i == IDX_FINDINGS) {
            snprintf(g_session[i], sizeof(g_session[i]),
                     "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":"
                     "{\"name\":\"omnicheck_findings\",\"arguments\":{\"paths\":[\"%s\"],"
                     "\"rule\":\"OC002\",\"limit\":2}}}",
                     fix);
        } else {
            snprintf(g_session[i], sizeof(g_session[i]), "%s", SESSION_JSONL[i]);
        }
        g_lines[n++] = g_session[i];
    }
    return n;
}

/* Runs one full session; returns the transcript (malloc'd) and the child
 * exit code. */
static char *run_session(const char *bin, const char *packs, const char **lines, size_t n,
                         int *exit_code) {
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0)
        return NULL;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execl(bin, "oc", "mcp", "--packs-dir", packs, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(lines[i]);
        ssize_t w = write(in_pipe[1], lines[i], len);
        if (w < 0)
            break;
        w = write(in_pipe[1], "\n", 1);
        if (w < 0)
            break;
    }
    close(in_pipe[1]);

    size_t cap = 1 << 16, len = 0;
    char *out = (char *)malloc(cap);
    if (!out)
        return NULL;
    size_t got;
    char chunk[4096];
    while ((got = (size_t)read(out_pipe[0], chunk, sizeof(chunk))) > 0) {
        if (len + got >= cap) {
            cap *= 2;
            char *grown = (char *)realloc(out, cap);
            if (!grown)
                break;
            out = grown;
        }
        memcpy(out + len, chunk, got);
        len += got;
    }
    close(out_pipe[0]);
    out[len] = '\0';

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    *exit_code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
    return out;
}

static void session_test(void) {
    const char *bin = OC_OC_BIN;
    const char *root = OC_REPO_ROOT;
    char packs[512], fix[512];
    snprintf(packs, sizeof(packs), "%s/packs", root);
    snprintf(fix, sizeof(fix), "%s/tests/fixtures_sprint4/lspdemo", root);

    size_t n = build_session(fix);
    int rc1 = 0, rc2 = 0;
    char *t1 = run_session(bin, packs, g_lines, n, &rc1);
    char *t2 = run_session(bin, packs, g_lines, n, &rc2);
    CHECK(t1 != NULL && t2 != NULL);
    CHECK(rc1 == 0); /* stdin EOF is a clean stop */
    CHECK(rc2 == 0);
    if (t1 && t2)
        CHECK(strcmp(t1, t2) == 0); /* deterministic transcript */

    /* Parse the transcript line by line and map responses by id. */
    oc_arena a;
    oc_arena_init(&a);
    int saw_init = 0, saw_tools = 0, saw_rules = 0, saw_scan_report = 0;
    int saw_findings_filtered = 0, saw_unknown_tool_err = 0, saw_unknown_method_err = 0;
    char *save = t1;
    for (char *line = strtok(t1, "\n"); line; line = strtok(NULL, "\n")) {
        oc_status st;
        oc_status_clear(&st);
        oc_json *m = oc_json_parse(&a, line, strlen(line), &st);
        if (!m)
            continue;
        double idn = oc_json_as_num(oc_json_get(m, "id"), -1);
        const oc_json *error = oc_json_get(m, "error");
        if (idn == 1) {
            const oc_json *result = oc_json_get(m, "result");
            CHECK_STR_EQ(oc_json_as_str(oc_json_get(result, "protocolVersion"), "?"), "2025-03-26");
            CHECK_STR_EQ(
                oc_json_as_str(oc_json_get(oc_json_get(result, "serverInfo"), "name"), "?"),
                "omnicheck");
            saw_init = 1;
        } else if (idn == 2) {
            const oc_json *tools = oc_json_get(oc_json_get(m, "result"), "tools");
            if (tools && oc_json_size(tools) == 3) {
                const char *names[3];
                for (size_t i = 0; i < 3; i++)
                    names[i] = oc_json_as_str(oc_json_get(tools->items[i], "name"), "?");
                if (strcmp(names[0], "omnicheck_scan") == 0 &&
                    strcmp(names[1], "omnicheck_findings") == 0 &&
                    strcmp(names[2], "omnicheck_rules") == 0)
                    saw_tools = 1;
                for (size_t i = 0; i < 3; i++) {
                    const oc_json *schema = oc_json_get(tools->items[i], "inputSchema");
                    CHECK_STR_EQ(oc_json_as_str(oc_json_get(schema, "type"), "?"), "object");
                }
            }
        } else if (idn == 3) {
            const oc_json *content = oc_json_get(oc_json_get(m, "result"), "content");
            if (content && oc_json_size(content) == 1) {
                const char *text = oc_json_as_str(oc_json_get(content->items[0], "text"), NULL);
                if (text) {
                    oc_json *body = oc_json_parse(&a, text, strlen(text), &st);
                    if (body) {
                        double count = oc_json_as_num(oc_json_get(body, "count"), 0);
                        const oc_json *rules = oc_json_get(body, "rules");
                        if (count >= 8 && rules && oc_json_size(rules) == (size_t)count) {
                            const char *first =
                                oc_json_as_str(oc_json_get(rules->items[0], "id"), "?");
                            if (strcmp(first, "OC001") == 0)
                                saw_rules = 1;
                        }
                    }
                }
            }
        } else if (idn == 4) {
            const oc_json *content = oc_json_get(oc_json_get(m, "result"), "content");
            if (content && oc_json_size(content) == 1) {
                const char *text = oc_json_as_str(oc_json_get(content->items[0], "text"), NULL);
                if (text) {
                    oc_json *report = oc_json_parse(&a, text, strlen(text), &st);
                    if (report) {
                        const oc_json *meta = oc_json_get(report, "meta");
                        const char *tool = oc_json_as_str(oc_json_get(meta, "tool"), "?");
                        double fc = oc_json_as_num(oc_json_get(meta, "finding_count"), 0);
                        if (strcmp(tool, "omnicheck") == 0 && fc >= 1)
                            saw_scan_report = 1;
                        /* The MCP scan text is the report contract: parse
                         * errors and triage blocks are present. */
                        CHECK(oc_json_get(report, "parse_errors") != NULL);
                        CHECK(oc_json_get(report, "triage") != NULL);
                    }
                }
            }
        } else if (idn == 5) {
            const oc_json *content = oc_json_get(oc_json_get(m, "result"), "content");
            if (content && oc_json_size(content) == 1) {
                const char *text = oc_json_as_str(oc_json_get(content->items[0], "text"), NULL);
                if (text) {
                    oc_json *body = oc_json_parse(&a, text, strlen(text), &st);
                    if (body) {
                        double count = oc_json_as_num(oc_json_get(body, "count"), -1);
                        const oc_json *findings = oc_json_get(body, "findings");
                        if (count >= 0 && (!findings || oc_json_size(findings) <= 2)) {
                            int all_oc002 = 1;
                            for (size_t i = 0; findings && i < oc_json_size(findings); i++) {
                                if (strcmp(oc_json_as_str(oc_json_get(findings->items[i], "rule"),
                                                          "?"),
                                           "OC002") != 0)
                                    all_oc002 = 0;
                            }
                            if (all_oc002)
                                saw_findings_filtered = 1;
                        }
                    }
                }
            }
        } else if (idn == 6 && error) {
            if (oc_json_as_num(oc_json_get(error, "code"), 0) == -32602)
                saw_unknown_tool_err = 1;
        } else if (idn == 7 && error) {
            if (oc_json_as_num(oc_json_get(error, "code"), 0) == -32601)
                saw_unknown_method_err = 1;
        }
    }
    (void)save;
    CHECK(saw_init);
    CHECK(saw_tools);
    CHECK(saw_rules);
    CHECK(saw_scan_report);
    CHECK(saw_findings_filtered);
    CHECK(saw_unknown_tool_err);
    CHECK(saw_unknown_method_err);
    oc_arena_destroy(&a);
    free(t1);
    free(t2);
}

int main(void) {
    session_test();
    TEST_SUMMARY();
}
