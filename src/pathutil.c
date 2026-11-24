#include "pathutil.h"

#include <stdio.h>
#include <string.h>

int oc_path_join(char *out, size_t cap, const char *dir, const char *name) {
    size_t dl = strlen(dir);
    if (dl == 0)
        return snprintf(out, cap, "%s", name) < (int)cap;
    /* strip trailing slash on dir (but keep "/" itself) */
    if (dl > 1 && dir[dl - 1] == '/')
        dl--;
    return snprintf(out, cap, "%.*s/%s", (int)dl, dir, name) < (int)cap;
}

size_t oc_path_normalize(char *path) {
    const char *seg[64];
    size_t seg_len[64];
    size_t depth = 0;
    int absolute = (path[0] == '/');
    char *p = path;
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        char *start = p;
        while (*p && *p != '/')
            p++;
        size_t len = (size_t)(p - start);
        if (len == 1 && start[0] == '.')
            continue; /* skip "." */
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (depth > 0)
                depth--; /* pop one resolved segment */
            continue;    /* at root, ".." stays at root (lexical) */
        }
        if (depth < sizeof(seg) / sizeof(seg[0])) {
            seg[depth] = start;
            seg_len[depth] = len;
            depth++;
        }
    }
    size_t off = 0;
    if (absolute)
        path[off++] = '/';
    if (depth == 0) {
        if (!absolute) {
            path[0] = '.';
            path[1] = '\0';
            return 1;
        }
        path[off] = '\0';
        return off;
    }
    for (size_t i = 0; i < depth; i++) {
        if (i)
            path[off++] = '/';
        memmove(path + off, seg[i], seg_len[i]);
        off += seg_len[i];
    }
    path[off] = '\0';
    return off;
}

void oc_path_dirname(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, cap, ".");
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len == 0)
        len = 1; /* "/x" -> "/" */
    if (len >= cap)
        len = cap - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

void oc_path_up(const char *path, size_t levels, char *out, size_t cap) {
    snprintf(out, cap, "%s", path);
    for (size_t i = 0; i < levels; i++) {
        const char *slash = strrchr(out, '/');
        if (!slash) {
            snprintf(out, cap, ".");
            return;
        }
        size_t len = (size_t)(slash - out);
        if (len == 0)
            len = 1;
        out[len] = '\0';
        if (strcmp(out, "/") == 0)
            return;
    }
}
