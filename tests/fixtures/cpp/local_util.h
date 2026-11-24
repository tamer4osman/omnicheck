// The root-default fixture (SPRINT-21): C-compatible header content --
// the sniff is inconclusive (clean under both grammars, no C++
// -distinctive node) and the project-root default (cpp sources in the
// discovered set) claims it for the cpp pack. The guard-wrapped
// declarations stay surface through the climb.
#ifndef LOCAL_UTIL_H
#define LOCAL_UTIL_H

int local_util(int n);

static inline int local_util_sq(int n) {
    return n * n;
}

#endif
