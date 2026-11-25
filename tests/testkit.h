/* Minimal test framework for OmniCheck unit tests (TAP-ish output). */
#ifndef OC_TESTKIT_H
#define OC_TESTKIT_H

#include <stdio.h>
#include <string.h>

static int oc__checks = 0;
static int oc__fails = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        oc__checks++;                                                                              \
        if (!(cond)) {                                                                             \
            oc__fails++;                                                                           \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
        }                                                                                          \
    } while (0)

#define CHECK_STR_EQ(a, b)                                                                         \
    do {                                                                                           \
        oc__checks++;                                                                              \
        const char *_a = (a);                                                                      \
        const char *_b = (b);                                                                      \
        if (!_a || !_b || strcmp(_a, _b) != 0) {                                                   \
            oc__fails++;                                                                           \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,                \
                    _a ? _a : "(null)", _b ? _b : "(null)");                                       \
        }                                                                                          \
    } while (0)

#define CHECK_STR_CONTAINS(hay, needle)                                                            \
    do {                                                                                           \
        oc__checks++;                                                                              \
        const char *_h = (hay);                                                                    \
        const char *_n = (needle);                                                                 \
        if (!_h || !_n || !strstr(_h, _n)) {                                                       \
            oc__fails++;                                                                           \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" lacks \"%s\"\n", __FILE__, __LINE__,             \
                    _h ? _h : "(null)", _n);                                                       \
        }                                                                                          \
    } while (0)

#define TEST_SUMMARY()                                                                             \
    do {                                                                                           \
        fprintf(stderr, "%s: %d checks, %d failures\n", __FILE__, oc__checks, oc__fails);          \
        return oc__fails ? 1 : 0;                                                                  \
    } while (0)

#endif /* OC_TESTKIT_H */
