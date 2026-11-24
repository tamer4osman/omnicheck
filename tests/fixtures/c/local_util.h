// The resolved include target (SPRINT-20): a conventional include guard
// wraps every declaration -- the top-level climb must see through it
// (the exports query matches candidates anywhere; the parent chain
// climbs preproc wrappers to translation_unit).
#ifndef LOCAL_UTIL_H
#define LOCAL_UTIL_H

int local_util(int n);

static inline int local_util_sq(int n) {
    return n * n;
}

#endif
