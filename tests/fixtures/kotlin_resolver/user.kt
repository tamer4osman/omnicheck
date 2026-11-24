// Resolver corpus B (SPRINT-22): the default-package claim -- the
// one-segment import resolves through the empty package clause (the
// declared-fact walk treats the missing package_header as the default
// package).
package app.user

import standalone

fun userFn(): Int {
    return standalone()
}
