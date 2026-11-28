#include "oc/triage.h"

#include "oc/json.h"
#include "oc/str.h"
#include "oc/triage_embed.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * OmniDecide triage: sidecar client + T2 embedded provider (rank-only,
 * fail-open; see triage.h for the protocol and guardrails). Sprint 0
 * ships the full client contract and validates it with
 * tools/mock_omnidecide.py; the production shim links omnidecide-c and
 * speaks the same one-line JSON protocol.
 *
 * T2 (OC_EMBED_OMNIDECIDE): the vendored kernel can be linked INTO lib
 * oc (--triage-embedded). The embedded path consumes the SAME
 * protocol-v0 request the sidecar would receive and produces the SAME
 * one-line response -- byte for byte (C1 contract, asserted by
 * tests/test_triage_embed.c). Both paths share ONE response-application
 * code (triage_consume_response below); only the provider label differs
 * ("embedded:omnidecide-X.Y.Z" vs the sidecar binary path). The sidecar
 * keeps ABSOLUTE precedence: with OC_OMNIDECIDE_BIN set, both --triage
 * and --triage-embedded go through the sidecar seam, so the seam stays
 * fully testable against an embedded build.
 *
 * Timeout discipline (reworked after the real OmniDecide kernel exposed
 * the original design's limits -- the mock always exited instantly, so a
 * one-shot alarm + EINTR-continue never actually bounded a slow provider):
 * ONE total budget (default 3 s, override OC_OMNIDECIDE_TIMEOUT_S, 1..600)
 * covers BOTH the request write and the response read, enforced with
 * poll() deadlines -- no signal can be missed. SIGPIPE is blocked for the
 * interaction (a provider that dies mid-request must yield a writable
 * EPIPE, not kill the scan), and the mask is restored in the child before
 * exec so providers keep their own SIGPIPE semantics. On timeout the
 * provider is killed (SIGKILL) before waitpid, so a silent provider can
 * never hang the scan.
 */

#define OC_TRIAGE_TIMEOUT_SECONDS 3
/* BUGFIX N-4: the response ceiling is now a generous GROWTH cap, not a
 * truncation point. The old fixed 64 KiB stack buffer silently cut
 * larger provider responses mid-JSON, which then failed to parse and
 * fail-opened with ZERO ranked findings -- rank coverage collapsed on
 * exactly the large runs where triage matters (the nodejs/node lib/
 * audit hit this: 1,926 findings, 928 ranked). The client now grows a
 * heap buffer until EOF or deadline; only a provider exceeding the cap
 * fails open, with a named reason. */
#define OC_TRIAGE_MAX_RESPONSE (64u * 1024u * 1024u)
#define OC_TRIAGE_MAX_BUDGET_S 600

static long long triage_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Provider budget in ms: OC_OMNIDECIDE_TIMEOUT_S override, 1..600 s,
 * default OC_TRIAGE_TIMEOUT_SECONDS. Malformed or out-of-range values
 * keep the default (fail-safe). */
static long long triage_budget_ms(void) {
    long long budget = (long long)OC_TRIAGE_TIMEOUT_SECONDS * 1000;
    const char *tb = getenv("OC_OMNIDECIDE_TIMEOUT_S");
    if (tb && tb[0]) {
        char *end = NULL;
        long v = strtol(tb, &end, 10);
        if (end && end != tb && *end == '\0' && v >= 1 && v <= OC_TRIAGE_MAX_BUDGET_S)
            budget = v * 1000;
    }
    return budget;
}

/* Builds {"op":"rank","items":[...]} from the findings vector. */
static int build_request(const oc_finding_vec *v, oc_strbuf *b) {
    oc_jsonw w;
    oc_jsonw_init(&w);
    oc_jsonw_obj_begin(&w);
    oc_jsonw_key(&w, "op");
    oc_jsonw_str(&w, "rank");
    oc_jsonw_key(&w, "items");
    oc_jsonw_arr_begin(&w);
    for (size_t i = 0; i < v->n; i++) {
        oc_jsonw_obj_begin(&w);
        oc_jsonw_key(&w, "id");
        oc_jsonw_str(&w, v->items[i].fingerprint);
        oc_jsonw_key(&w, "text");
        oc_jsonw_str(&w, v->items[i].message);
        oc_jsonw_obj_end(&w);
    }
    oc_jsonw_arr_end(&w);
    oc_jsonw_obj_end(&w);
    oc_strbuf_appends(b, w.buf.data ? w.buf.data : "");
    int ok = w.buf.data != NULL;
    oc_jsonw_destroy(&w);
    return ok;
}

static void fail_open(const char *bin, oc_triage_outcome *out, const char *why) {
    out->ranked = 0;
    snprintf(out->provider, sizeof(out->provider), "%s", bin ? bin : "none");
    snprintf(out->reason, sizeof(out->reason), "%s", why);
}

/* T2: shared response consumer for BOTH provider paths (C1 protocol
 * equality): parse the one-line protocol-v0 response, annotate
 * rank+confidence (rank-only, never drop/reorder/re-severity), count
 * DISTINCT annotated findings honestly and set the outcome. The two
 * reason strings arrive verbatim per path so each keeps its exact
 * historical report bytes (the byte-stability guarantee: only the
 * provider label differs between sidecar and embedded runs). */
static void triage_consume_response(const char *resp, size_t resp_len, const char *provider,
                                    const char *invalid_json_reason, const char *no_ranks_reason,
                                    oc_finding_vec *v, oc_triage_outcome *out) {
    /* Parse with a local throwaway arena. */
    oc_arena tmp;
    oc_arena_init(&tmp);
    oc_arena_set_block_size(&tmp, 8 * 1024);
    oc_status parse_st;
    oc_status_clear(&parse_st);
    oc_json *root = oc_json_parse(&tmp, resp, resp_len, &parse_st);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_arena_destroy(&tmp);
        fail_open(provider, out, invalid_json_reason);
        return;
    }
    const char *model = oc_json_as_str(oc_json_get(root, "model"), "");
    snprintf(out->model, sizeof(out->model), "%s", model);
    const oc_json *ranks = oc_json_get(root, "ranks");
    if (ranks && ranks->type == OC_JSON_ARR) {
        for (size_t i = 0; i < ranks->n_items; i++) {
            const oc_json *r = ranks->items[i];
            const char *id = oc_json_as_str(oc_json_get(r, "id"), NULL);
            if (!id)
                continue;
            double rank = oc_json_as_num(oc_json_get(r, "rank"), -1.0);
            double conf = oc_json_as_num(oc_json_get(r, "confidence"), 0.0);
            for (size_t j = 0; j < v->n; j++) {
                if (strcmp(v->items[j].fingerprint, id) == 0) {
                    /* Rank-only guardrail: annotate, never remove.
                     * Annotate EVERY finding carrying this fingerprint:
                     * fingerprints deliberately exclude line numbers, so
                     * byte-identical siblings (same rule, file, anchor,
                     * snippet) share one identity and must all be
                     * annotated, not just the first match. */
                    v->items[j].triaged = 1;
                    v->items[j].triage_rank = (rank >= 0.0) ? (int)rank : -1;
                    v->items[j].triage_confidence = conf;
                }
            }
        }
    }
    oc_arena_destroy(&tmp);

    /* ranked_count counts DISTINCT findings annotated, never response
     * entries: a provider that repeats an id (or identical siblings that
     * share one fingerprint) must not inflate the honest meta counter. */
    for (size_t j = 0; j < v->n; j++)
        if (v->items[j].triaged)
            out->ranked_count++;

    if (out->ranked_count == 0) {
        fail_open(provider, out, no_ranks_reason);
    } else {
        out->ranked = 1;
        snprintf(out->provider, sizeof(out->provider), "%s", provider);
        snprintf(out->reason, sizeof(out->reason), "ok");
    }
}

#if defined(OC_EMBED_OMNIDECIDE)
/* T2: embedded provider path. One in-process call; the same budget is
 * enforced as an honest post-check (a kernel run that blew its budget
 * fail-opens instead of quietly shipping a late rank). */
static oc_code triage_run_embedded(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st) {
    char label[64];
    oc_triage_embed_provider_label(label, sizeof(label));
    if (v->n == 0) {
        fail_open(label, out, "no findings to rank");
        oc_status_clear(st);
        return OC_OK;
    }
    oc_strbuf ereq;
    oc_strbuf_init(&ereq);
    if (!build_request(v, &ereq)) {
        oc_strbuf_destroy(&ereq);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building triage request");
        return OC_ERR_ALLOC;
    }
    long long edeadline = triage_now_ms() + triage_budget_ms();
    char *eresp = NULL;
    size_t eresp_len = 0;
    char ewhy[192];
    ewhy[0] = '\0';
    int eok = oc_triage_embed_respond(ereq.data, ereq.len, &eresp, &eresp_len, ewhy, sizeof(ewhy));
    oc_strbuf_destroy(&ereq);
    if (!eok) {
        /* T7: an explicitly configured resource the kernel refuses is
         * LOUD (named usage error), never a silent fail-open. */
        if (strncmp(ewhy, "kernel rejected ", 16) == 0) {
            oc_status_set(st, OC_ERR_USAGE, "%s", ewhy);
            return OC_ERR_USAGE;
        }
        fail_open(label, out, ewhy[0] ? ewhy : "embedded provider failed (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    if (triage_now_ms() > edeadline) {
        free(eresp);
        fail_open(label, out, "embedded triage exceeded its time budget (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    triage_consume_response(eresp, eresp_len, label,
                            "embedded provider response was not valid JSON",
                            "embedded provider returned no usable ranks", v, out);
    free(eresp);
    oc_status_clear(st);
    return OC_OK;
}
#endif /* OC_EMBED_OMNIDECIDE */

/* Sidecar path: spawn $OC_OMNIDECIDE_BIN, one request line in, one
 * response line out (historical flow; the response consumer is now the
 * shared T2 code so both paths apply responses identically). */
static oc_code triage_run_sidecar(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st,
                                  const char *bin) {
    if (v->n == 0) {
        fail_open(bin, out, "no findings to rank");
        oc_status_clear(st);
        return OC_OK;
    }

    oc_strbuf req;
    oc_strbuf_init(&req);
    if (!build_request(v, &req)) {
        oc_strbuf_destroy(&req);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory building triage request");
        return OC_ERR_ALLOC;
    }

    long long budget_ms = triage_budget_ms();
    long long deadline = triage_now_ms() + budget_ms;

    /* SIGPIPE blocked for the interaction: a provider that dies while we
     * still hold unwritten request bytes must yield EPIPE (write_failed
     * below), never terminate the scan. Restored in the child before exec
     * and in the parent after waitpid. */
    sigset_t pipe_mask, old_mask;
    sigemptyset(&pipe_mask);
    sigaddset(&pipe_mask, SIGPIPE);
    sigprocmask(SIG_BLOCK, &pipe_mask, &old_mask);

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        oc_strbuf_destroy(&req);
        fail_open(bin, out, "pipe() failed");
        oc_status_clear(st);
        return OC_OK;
    }

    pid_t pid = fork();
    if (pid < 0) {
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        oc_strbuf_destroy(&req);
        fail_open(bin, out, "fork() failed");
        oc_status_clear(st);
        return OC_OK;
    }

    if (pid == 0) {
        /* Child: own process group (so a timeout kills the provider's
         * whole tree, not just the shell -- same discipline as the
         * mutation engine's runner), restore the signal mask, wire the
         * pipes, exec. */
        setpgid(0, 0);
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        execl(bin, bin, (char *)NULL);
        _exit(127); /* exec failed: parent sees immediate EOF */
    }

    /* Parent. */
    setpgid(pid, pid); /* matches the child's own setpgid; benign if raced */
    close(in_pipe[0]);
    close(out_pipe[1]);

    size_t wrote = 0;
    int write_failed = 0;
    int timed_out = 0;
    while (wrote < req.len) {
        long long remaining = deadline - triage_now_ms();
        if (remaining <= 0) {
            timed_out = 1;
            break;
        }
        struct pollfd p = {.fd = in_pipe[1], .events = POLLOUT, .revents = 0};
        int pr = poll(&p, 1, (int)remaining);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            write_failed = 1;
            break;
        }
        if (pr == 0) {
            timed_out = 1;
            break;
        }
        ssize_t r = write(in_pipe[1], req.data + wrote, req.len - wrote);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            write_failed = 1;
            break;
        }
        if (r == 0) {
            write_failed = 1;
            break;
        }
        wrote += (size_t)r;
    }
    close(in_pipe[1]);

    /* BUGFIX N-4: dynamically grown response buffer (see the cap comment
     * above). Growth is deadline-bounded by the poll loop below; the
     * hard cap only trips for a runaway provider, which fails open with
     * a named reason instead of silently truncating. */
    size_t resp_cap = 64 * 1024;
    size_t resp_len = 0;
    int resp_overflow = 0;
    char *resp = (char *)malloc(resp_cap);
    int status = 0; /* shared: read phase OOM path and the waitpid below */
    if (!resp) {
        close(out_pipe[0]);
        kill(-pid, SIGKILL);
        waitpid(pid, &status, 0);
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        oc_strbuf_destroy(&req);
        fail_open(bin, out, "out of memory reading triage response (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    for (;;) {
        if (resp_len + 1 >= resp_cap) {
            if (resp_cap >= OC_TRIAGE_MAX_RESPONSE) {
                resp_overflow = 1;
                break;
            }
            size_t ncap = resp_cap * 2;
            if (ncap > OC_TRIAGE_MAX_RESPONSE)
                ncap = OC_TRIAGE_MAX_RESPONSE;
            char *nb = (char *)realloc(resp, ncap);
            if (!nb) {
                resp_overflow = 1;
                break;
            }
            resp = nb;
            resp_cap = ncap;
        }
        long long remaining = deadline - triage_now_ms();
        if (remaining <= 0) {
            timed_out = 1;
            break;
        }
        struct pollfd p = {.fd = out_pipe[0], .events = POLLIN, .revents = 0};
        int pr = poll(&p, 1, (int)remaining);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pr == 0) {
            timed_out = 1;
            break;
        }
        ssize_t r = read(out_pipe[0], resp + resp_len, resp_cap - 1 - resp_len);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0)
            break;
        resp_len += (size_t)r;
    }
    close(out_pipe[0]);

    /* A timed-out or unwritable provider may still be alive, possibly
     * with grandchildren holding the pipe ends; SIGKILL the whole process
     * group so waitpid below can never block and no orphan survives.
     * Harmless if it already exited. */
    if (timed_out || write_failed)
        kill(-pid, SIGKILL);
    waitpid(pid, &status, 0);
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    oc_strbuf_destroy(&req);

    /* Guardrail: any provider failure leaves findings unranked, run passes. */
    int exec_failed = (WIFEXITED(status) && WEXITSTATUS(status) == 127);
    if (timed_out) {
        free(resp);
        fail_open(bin, out, "triage timed out; provider killed (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    if (resp_overflow) {
        free(resp);
        fail_open(bin, out, "triage response exceeded the 64 MiB cap (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    if (write_failed || exec_failed) {
        free(resp);
        fail_open(bin, out,
                  exec_failed ? "sidecar binary failed to start" : "failed to send request");
        oc_status_clear(st);
        return OC_OK;
    }

    resp[resp_len] = '\0';
    triage_consume_response(resp, resp_len, bin, "sidecar response was not valid JSON",
                            "sidecar returned no usable ranks", v, out);
    free(resp);
    oc_status_clear(st);
    return OC_OK;
}

oc_code oc_triage_rank_findings(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st) {
    return oc_triage_rank_findings_mode(v, out, st, OC_TRIAGE_MODE_AUTO);
}

oc_code oc_triage_rank_findings_mode(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st,
                                     oc_triage_mode mode) {
    memset(out, 0, sizeof(*out));
    out->provider[0] = '\0';
    out->ranked = 0;

    const char *bin = getenv("OC_OMNIDECIDE_BIN");
    if (bin && bin[0]) {
        /* Absolute sidecar precedence (both --triage and --triage-embedded):
         * an explicitly configured sidecar always wins, so the seam stays
         * testable against an embedded build. EXCEPT: T7 kernel resources
         * have no sidecar channel -- an explicit request is refused loud
         * rather than silently ignored. */
        if (oc_triage_embed_files_requested()) {
            oc_status_set(st, OC_ERR_USAGE,
                          "--triage-lexicon/--triage-model require the embedded "
                          "kernel; the sidecar protocol-v0 has no resource "
                          "channel (unset OC_OMNIDECIDE_BIN or drop the "
                          "resource flags)");
            return OC_ERR_USAGE;
        }
        return triage_run_sidecar(v, out, st, bin);
    }
    if (mode == OC_TRIAGE_MODE_EMBEDDED) {
#if defined(OC_EMBED_OMNIDECIDE)
        /* Explicit request: a contradictory runtime opt-out is refused
         * loud (the caller asked for the embedded kernel by name). */
        const char *em = getenv("OC_OMNIDECIDE_EMBED");
        if (em && (strcmp(em, "off") == 0 || strcmp(em, "0") == 0)) {
            oc_status_set(st, OC_ERR_USAGE,
                          "--triage-embedded requested but OC_OMNIDECIDE_EMBED=%s "
                          "disables the embedded kernel (fail loud)",
                          em);
            return OC_ERR_USAGE;
        }
        return triage_run_embedded(v, out, st);
#else
        oc_status_set(st, OC_ERR_USAGE,
                      "--triage-embedded requires a build with "
                      "-DOC_EMBED_OMNIDECIDE=ON; this build has no embedded kernel");
        return OC_ERR_USAGE;
#endif
    }
    fail_open("none", out, "OC_OMNIDECIDE_BIN not set; triage disabled (fail-open)");
    oc_status_clear(st);
    return OC_OK;
}

oc_code oc_triage_explain_attach(oc_finding_vec *v, oc_triage_outcome *out, oc_status *st) {
    out->explain_requested = 1;
    out->explained_count = 0;
    snprintf(out->explain_reason, sizeof(out->explain_reason), "ok");

    if (!out->ranked) {
        snprintf(out->explain_reason, sizeof(out->explain_reason),
                 "triage did not rank; nothing to explain (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    if (strncmp(out->provider, "embedded:", 9) != 0) {
        snprintf(out->explain_reason, sizeof(out->explain_reason),
                 "rationale channels require the embedded kernel "
                 "(--triage-embedded); the sidecar protocol-v0 carries no "
                 "rationale");
        oc_status_clear(st);
        return OC_OK;
    }
    if (v->n < 2) {
        snprintf(out->explain_reason, sizeof(out->explain_reason),
                 "rank primitive needs at least 2 options; %zu finding(s)", v->n);
        oc_status_clear(st);
        return OC_OK;
    }

    /* The SAME request bytes the rank call consumed (build_request is
     * deterministic over the findings vector), so the explain pass sees
     * exactly the options the ranking saw. */
    oc_strbuf req;
    oc_strbuf_init(&req);
    if (!build_request(v, &req)) {
        oc_strbuf_destroy(&req);
        snprintf(out->explain_reason, sizeof(out->explain_reason),
                 "out of memory building the explain request (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    char *eresp = NULL;
    size_t eresp_len = 0;
    char ewhy[192];
    ewhy[0] = '\0';
    int eok = oc_triage_embed_explain(req.data, req.len, &eresp, &eresp_len, ewhy, sizeof(ewhy));
    oc_strbuf_destroy(&req);
    if (!eok) {
        snprintf(out->explain_reason, sizeof(out->explain_reason), "%s",
                 ewhy[0] ? ewhy : "embedded explain failed (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }

    /* Attach channels by fingerprint (annotate EVERY matching finding,
     * same sibling discipline as the rank consumer). */
    oc_arena tmp;
    oc_arena_init(&tmp);
    oc_arena_set_block_size(&tmp, 8 * 1024);
    oc_status parse_st;
    oc_status_clear(&parse_st);
    oc_json *root = oc_json_parse(&tmp, eresp, eresp_len, &parse_st);
    free(eresp);
    if (!root || root->type != OC_JSON_OBJ) {
        oc_arena_destroy(&tmp);
        snprintf(out->explain_reason, sizeof(out->explain_reason),
                 "embedded explain response was not valid JSON (fail-open)");
        oc_status_clear(st);
        return OC_OK;
    }
    const oc_json *items = oc_json_get(root, "explain");
    if (items && items->type == OC_JSON_ARR) {
        for (size_t i = 0; i < items->n_items; i++) {
            const oc_json *e = items->items[i];
            const char *id = oc_json_as_str(oc_json_get(e, "id"), NULL);
            if (!id)
                continue;
            for (size_t j = 0; j < v->n; j++) {
                if (strcmp(v->items[j].fingerprint, id) != 0)
                    continue;
                v->items[j].explained = 1;
                v->items[j].explain.probability =
                    oc_json_as_num(oc_json_get(e, "probability"), 0.0);
                v->items[j].explain.fused = oc_json_as_num(oc_json_get(e, "fused"), 0.0);
                v->items[j].explain.tf_cosine = oc_json_as_num(oc_json_get(e, "tf_cosine"), 0.0);
                v->items[j].explain.jaccard = oc_json_as_num(oc_json_get(e, "jaccard"), 0.0);
                v->items[j].explain.trigram = oc_json_as_num(oc_json_get(e, "trigram"), 0.0);
                v->items[j].explain.whole = oc_json_as_num(oc_json_get(e, "whole"), 0.0);
                v->items[j].explain.best_chunk = oc_json_as_num(oc_json_get(e, "best_chunk"), 0.0);
                v->items[j].explain.contextual = oc_json_as_num(oc_json_get(e, "contextual"), 0.0);
                v->items[j].explain.directional =
                    oc_json_as_num(oc_json_get(e, "directional"), 0.0);
            }
        }
    }
    oc_arena_destroy(&tmp);

    for (size_t j = 0; j < v->n; j++)
        if (v->items[j].explained)
            out->explained_count++;
    oc_status_clear(st);
    return OC_OK;
}
