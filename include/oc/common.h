/*
 * OmniCheck common definitions: status codes, version strings, small
 * cross-module utilities. Every module header includes this one.
 *
 * License: MIT. OmniCheck is a working name (see docs/SPRINT-0.md).
 */
#ifndef OC_COMMON_H
#define OC_COMMON_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OC_VERSION_STRING "0.3.0"
#define OC_REPORT_SCHEMA  "omnicheck.report/v0"
#define OC_PACK_SCHEMA    "omnicheck.pack/v0"

typedef enum oc_code {
    OC_OK = 0,
    OC_ERR_ALLOC = 1,
    OC_ERR_IO = 2,
    OC_ERR_PARSE = 3,
    OC_ERR_PACK = 4,
    OC_ERR_USAGE = 5,
    OC_ERR_INTERNAL = 6,
    OC_ERR_TRIAGE = 7
} oc_code;

const char *oc_code_str(oc_code code);

/* Error channel: code plus a human-readable message. Functions that can
 * fail take a nullable oc_status*; when NULL, messages are discarded. */
typedef struct oc_status {
    oc_code code;
    char msg[192];
} oc_status;

void oc_status_set(oc_status *st, oc_code code, const char *fmt, ...);
void oc_status_clear(oc_status *st);

/* Read an entire file into a NUL-terminated heap buffer (caller free()s).
 * *out_len receives the byte length excluding the terminator. */
oc_code oc_read_file(const char *path, char **out_data, size_t *out_len, oc_status *st);

/* Case-insensitive suffix test; ext must include the leading dot. */
int oc_path_has_ext(const char *path, const char *ext);

#ifdef __cplusplus
}
#endif
#endif /* OC_COMMON_H */
