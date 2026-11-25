#include "oc/common.h"

#include <stdio.h>
#include <stdlib.h>

void oc_status_set(oc_status *st, oc_code code, const char *fmt, ...) {
    if (!st)
        return;
    st->code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(st->msg, sizeof(st->msg), fmt, ap);
    va_end(ap);
}

void oc_status_clear(oc_status *st) {
    if (!st)
        return;
    st->code = OC_OK;
    st->msg[0] = '\0';
}

const char *oc_code_str(oc_code code) {
    switch (code) {
    case OC_OK:
        return "ok";
    case OC_ERR_ALLOC:
        return "alloc";
    case OC_ERR_IO:
        return "io";
    case OC_ERR_PARSE:
        return "parse";
    case OC_ERR_PACK:
        return "pack";
    case OC_ERR_USAGE:
        return "usage";
    case OC_ERR_INTERNAL:
        return "internal";
    case OC_ERR_TRIAGE:
        return "triage";
    }
    return "unknown";
}

oc_code oc_read_file(const char *path, char **out_data, size_t *out_len, oc_status *st) {
    *out_data = NULL;
    *out_len = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        oc_status_set(st, OC_ERR_IO, "cannot open '%s'", path);
        return OC_ERR_IO;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        oc_status_set(st, OC_ERR_IO, "seek failed on '%s'", path);
        return OC_ERR_IO;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        oc_status_set(st, OC_ERR_IO, "ftell failed on '%s'", path);
        return OC_ERR_IO;
    }
    rewind(f);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        oc_status_set(st, OC_ERR_ALLOC, "out of memory reading '%s'", path);
        return OC_ERR_ALLOC;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        oc_status_set(st, OC_ERR_IO, "short read on '%s'", path);
        return OC_ERR_IO;
    }
    buf[sz] = '\0';
    *out_data = buf;
    *out_len = (size_t)sz;
    return OC_OK;
}

int oc_path_has_ext(const char *path, const char *ext) {
    size_t pl = strlen(path);
    size_t el = strlen(ext);
    if (el == 0 || pl < el)
        return 0;
    const char *tail = path + (pl - el);
    for (size_t i = 0; i < el; i++) {
        char a = tail[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return 1;
}
