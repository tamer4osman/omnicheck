/* OC002 todo-comment -- comment-marker audit (capability layer T5).
 * Case-insensitive scan for TODO / FIXME / HACK markers in doc comments.
 * The normalized marker text is folded into the fingerprint so sibling
 * markers stay distinct without depending on volatile line numbers. */
#include "passes.h"

#include <ctype.h>

static const char *const MARKERS[] = {"TODO", "FIXME", "HACK"};

static const char *contains_marker_ci(const char *text, const char **marker_out) {
    size_t tlen = strlen(text);
    for (size_t mi = 0; mi < sizeof(MARKERS) / sizeof(MARKERS[0]); mi++) {
        const char *kw = MARKERS[mi];
        size_t klen = strlen(kw);
        for (size_t i = 0; i + klen <= tlen; i++) {
            size_t j = 0;
            while (j < klen && tolower((unsigned char)text[i + j]) == tolower((unsigned char)kw[j]))
                j++;
            if (j == klen) {
                /* Marker must not be glued into a longer identifier. */
                char before = i > 0 ? text[i - 1] : ' ';
                char after = (i + klen < tlen) ? text[i + klen] : ' ';
                int before_ok = !(isalnum((unsigned char)before) || before == '_');
                int after_ok = !(isalnum((unsigned char)after) || after == '_');
                if (before_ok && after_ok) {
                    *marker_out = kw;
                    return text + i;
                }
            }
        }
    }
    return NULL;
}

static oc_code run_todo_comment(const oc_pass_ctx *ctx, oc_status *st) {
    if (!ctx->doc) {
        oc_status_clear(st);
        return OC_OK;
    }
    const oc_node *file = &ctx->graph->nodes[ctx->file_id];

    for (size_t i = 0; i < ctx->doc->n_comments; i++) {
        const oc_comment *c = &ctx->doc->comments[i];
        const char *marker = NULL;
        if (!contains_marker_ci(c->text, &marker))
            continue;

        char norm[128];
        oc_fingerprint_normalize(c->text, norm, sizeof(norm));
        char *msg = oc_arena_sprintf(ctx->arena, "%s marker in comment: \"%s\"", marker, norm);
        if (!msg) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory formatting finding");
            return OC_ERR_ALLOC;
        }
        oc_finding_spec spec;
        memset(&spec, 0, sizeof(spec));
        spec.rule_id = "OC002";
        spec.rule_name = "todo-comment";
        spec.severity = OC_SEV_INFO;
        spec.file = file->path;
        spec.line = c->line;
        spec.col = c->col;
        spec.end_line = c->line;
        spec.symbol = NULL;
        spec.message = msg;
        spec.fp_text = norm;
        if (!oc_findings_push(ctx->out, ctx->arena, &spec)) {
            oc_status_set(st, OC_ERR_ALLOC, "out of memory appending finding");
            return OC_ERR_ALLOC;
        }
    }
    oc_status_clear(st);
    return OC_OK;
}

const oc_pass oc_pass_todo_comment = {
    "OC002",          "todo-comment",
    OC_SEV_INFO,      "Comment contains an open TODO / FIXME / HACK marker",
    run_todo_comment,
};
