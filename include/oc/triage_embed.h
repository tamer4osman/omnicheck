/*
 * Embedded OmniDecide triage provider (T2, plan feature OC_EMBED_OMNIDECIDE).
 *
 * When OmniCheck is built with -DOC_EMBED_OMNIDECIDE=ON, the vendored
 * OmniDecide-C kernel is linked INTO lib oc and triage runs in-process:
 * no fork/exec, no sidecar process. The wire contract is byte-identical
 * to the sidecar (C1 resolution): this module consumes the same
 * protocol-v0 request line that src/triage.c sends to $OC_OMNIDECIDE_BIN
 * and produces the same one-line protocol-v0 response the
 * oc-omnidecide-provider shim would print on stdout -- byte for byte,
 * because it mirrors tools/omnidecide/oc_odk_provider.c call for call:
 * same fixed instruction, same ODK_LANG_EN kernel config, same rank
 * primitive, same ranked-order walk, same "%.4f" confidence rendering,
 * same emit cap and same fail-open-on-error discipline.
 *
 * Guardrails (identical to include/oc/triage.h):
 *   rank-only     : this module only ranks; it cannot drop, reorder or
 *                   re-severity findings.
 *   prose-only    : the only per-finding data used is the message text,
 *                   passed as rank options; the context channel is empty.
 *   fail-open     : on ANY error this module returns 0 with a named
 *                   reason and the scan proceeds unranked. It NEVER fails
 *                   the scan.
 *   determinism   : the kernel is documented bit-identical
 *                   (-ffp-contract=off upstream); responses for the same
 *                   request bytes are byte-stable, so the embedded path
 *                   preserves OmniCheck's byte-level output contract --
 *                   the ONLY visible difference is the provider label
 *                   ("embedded:omnidecide-X.Y.Z" instead of the sidecar
 *                   binary path) printed in triage metadata.
 */
#ifndef OC_TRIAGE_EMBED_H
#define OC_TRIAGE_EMBED_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Provider label reported in triage metadata, built from the vendored
 * kernel's version macros (e.g. "embedded:omnidecide-2.0.0"). */
void oc_triage_embed_provider_label(char *buf, size_t sz);

/* T7 (opt-in): kernel customization files for the NEXT responder call.
 * Lexicon packs load with odk_kernel_load_lexicon and an optional model
 * file with odk_kernel_load_model, right after kernel creation, before
 * any ranking. Per the T7 contract a file the kernel REFUSES is LOUD:
 * oc_triage_embed_respond / oc_triage_embed_explain return 0 with a
 * reason beginning "kernel rejected " naming the path and the kernel's
 * own diagnostic, which triage.c maps to a loud usage error (exit 2) --
 * an explicitly configured resource is never silently skipped. The
 * pointers must stay valid until the next responder call. Passing
 * NULL/0 clears the customization. */
void oc_triage_embed_set_kernel_files(const char *const *lexicon_paths, size_t n_lexicons,
                                      const char *model_path);
size_t oc_triage_embed_lexicons_applied(void);
int oc_triage_embed_model_applied(void);

/* Non-zero when customization files are pending (lexicons or a model):
 * the triage dispatcher uses this to refuse the SIDEcar path loud -- the
 * sidecar protocol-v0 has no resource channel. */
int oc_triage_embed_files_requested(void);

/* Consume ONE protocol-v0 request:
 *   {"op":"rank","items":[{"id":"...","text":"..."}, ...]}
 * and produce the ONE protocol-v0 response line:
 *   {"model":"omnidecide-c/X.Y.Z","ranks":[{"id":"...","rank":0,
 *       "confidence":0.42}, ...]}
 * with a trailing '\n'.
 *
 * Returns 1 and sets *resp (malloc'd, NUL-terminated; caller frees) and
 * *resp_len (excludes the NUL) on success. Returns 0 on the fail-open
 * paths (malformed request, kernel or rank failure, emit-cap trip),
 * writing a named human reason into reason/reason_sz; *resp is then left
 * untouched (NULL). oc_triage_embed_respond never fails the scan: the
 * caller maps a 0 return to the standard fail-open outcome. */
int oc_triage_embed_respond(const char *req, size_t req_len, char **resp, size_t *resp_len,
                            char *reason, size_t reason_sz);

/* T1: explain responder -- same request bytes as oc_triage_embed_respond,
 * but the kernel's read-only explain pass (odk_explain_choice) runs over
 * ALL items and the response document carries the 9 rationale channels
 * per id:
 *   {"model":"omnidecide-c/X.Y.Z","explain":[
 *       {"id":"...","probability":0.62,"fused":...,"tf_cosine":...,
 *        "jaccard":...,"trigram":...,"whole":...,"best_chunk":...,
 *        "contextual":...,"directional":...}, ...]}
 * in REQUEST order (stable mapping onto the caller's items; the ranked
 * order is the rank call's business, this document never re-ranks).
 * probability uses %.4f, the other channels %.17g -- the reference
 * formats of the explain auditor. Same return/reason discipline as
 * oc_triage_embed_respond: 1 + malloc'd resp on success, 0 + named
 * reason on any fail-open path. */
int oc_triage_embed_explain(const char *req, size_t req_len, char **resp, size_t *resp_len,
                            char *reason, size_t reason_sz);

#ifdef __cplusplus
}
#endif
#endif /* OC_TRIAGE_EMBED_H */
