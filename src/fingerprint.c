#include "oc/fingerprint.h"

#include "oc/str.h"

#include <ctype.h>

/* Length-prefixes every field so that ("ab","c") and ("a","bc") hash
 * differently; separators disambiguate empty fields. */
static uint64_t fingerprint_fields(const char *const *fields, size_t n) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++) {
        const char *f = fields[i] ? fields[i] : "";
        size_t len = strlen(f);
        unsigned char lb[4];
        lb[0] = (unsigned char)(len & 0xFFu);
        lb[1] = (unsigned char)((len >> 8) & 0xFFu);
        lb[2] = (unsigned char)((len >> 16) & 0xFFu);
        lb[3] = (unsigned char)((len >> 24) & 0xFFu);
        for (int j = 0; j < 4; j++) {
            h ^= lb[j];
            h *= 1099511628211ULL;
        }
        for (size_t k = 0; k < len; k++) {
            h ^= (unsigned char)f[k];
            h *= 1099511628211ULL;
        }
        h ^= 0x1Fu;
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t oc_fingerprint_u64(const char *rule_id, const char *file, const char *anchor,
                            const char *normalized_snippet) {
    const char *fields[4] = {rule_id, file, anchor, normalized_snippet};
    /* An absent or empty snippet folds to the same 3-field domain. */
    size_t n = (normalized_snippet && normalized_snippet[0]) ? 4 : 3;
    return fingerprint_fields(fields, n);
}

void oc_fingerprint_hex(uint64_t h, char out[OC_FINGERPRINT_HEX_LEN + 1]) {
    static const char hexdig[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        unsigned char byte = (unsigned char)((h >> (56 - 8 * i)) & 0xFFu);
        out[2 * i] = hexdig[byte >> 4];
        out[2 * i + 1] = hexdig[byte & 0x0F];
    }
    out[OC_FINGERPRINT_HEX_LEN] = '\0';
}

void oc_fingerprint_normalize(const char *text, char *out, size_t out_cap) {
    if (out_cap == 0)
        return;
    size_t o = 0;
    const char *p = text ? text : "";
    int in_space = 0;
    while (*p && o + 1 < out_cap) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in_space = 1;
            p++;
            continue;
        }
        if (in_space && o > 0 && o + 1 < out_cap) {
            out[o++] = ' ';
        }
        in_space = 0;
        out[o++] = (char)c;
        p++;
    }
    out[o] = '\0';
}
