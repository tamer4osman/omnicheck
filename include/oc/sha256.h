/* Public-domain-style compact SHA-256 (FIPS 180-4). Deterministic,
 * allocation-free, no external dependencies. Used by the T7 opt-in
 * kernel-file integrity pins (lexicon packs are pinned by content
 * hash; the kernel itself validates the file contents). */
#ifndef OC_SHA256_H
#define OC_SHA256_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Computes the 64-character lowercase hex digest of len bytes. */
void oc_sha256_hex(const unsigned char *data, size_t len, char *out_hex /* 65 bytes */);

#ifdef __cplusplus
}
#endif
#endif /* OC_SHA256_H */
