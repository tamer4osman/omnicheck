/*
 * Internal path helpers shared by the resolver stack (imports.c,
 * tsconfig.c, surface.c). Purely lexical, deterministic, no IO.
 */
#ifndef OC_PATHUTIL_H
#define OC_PATHUTIL_H

#include <stddef.h>

/* Copies up to cap-1 bytes of dir+name into out; returns 0 on overflow.
 * dir may be "" or "."; a trailing slash on dir is stripped. */
int oc_path_join(char *out, size_t cap, const char *dir, const char *name);

/* Lexical in-place normalization of a relative or absolute path:
 * collapses "./" segments, resolves ".." against the previous segment,
 * squeezes duplicate slashes, and PRESERVES a leading "/" (absolute
 * paths must stay absolute or they will never match a graph node).
 * Returns the resulting length. */
size_t oc_path_normalize(char *path);

/* dirname: finds the last '/' and truncates; "." when none. */
void oc_path_dirname(const char *path, char *out, size_t cap);

/* Applies exactly n levels of ".." (i.e. strips n trailing components).
 * "a/b/c" minus 1 = "a/b"; minus more components than exist = ".". */
void oc_path_up(const char *path, size_t levels, char *out, size_t cap);

#endif /* OC_PATHUTIL_H */
