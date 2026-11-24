/*
 * Stable finding fingerprints (plan Ch4 determinism contract).
 *
 * FNV-1a 64-bit over length-prefixed fields:
 *   rule_id \0 file \0 anchor \0 normalized_snippet?
 *
 * Deliberately EXCLUDED: line/column numbers. A finding survives code
 * motion (same rule, same file, same symbol => same fingerprint) while
 * remaining distinct per rule/file/anchor combination. For anchor-less
 * passes (e.g. comment markers) pass a normalized snippet as fp_text so
 * sibling findings stay distinct without depending on line numbers.
 */
#ifndef OC_FINGERPRINT_H
#define OC_FINGERPRINT_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_FINGERPRINT_HEX_LEN 16

uint64_t oc_fingerprint_u64(const char *rule_id, const char *file, const char *anchor,
                            const char *normalized_snippet);
void oc_fingerprint_hex(uint64_t h, char out[OC_FINGERPRINT_HEX_LEN + 1]);

/* Trim, collapse internal whitespace, cap at 120 chars. */
void oc_fingerprint_normalize(const char *text, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
#endif /* OC_FINGERPRINT_H */
