// Resolver corpus B (SPRINT-22): the member import (Engine.start) -- the
// ambiguous member shape resolves BOTH readings; only the object reading
// matches here (package app.core declares Engine), so the edge lands on
// core.kt; the record keeps the NULL bind (whole-file claim).
package app.util

import app.core.Engine.start

fun memb(): Int {
    return 1
}
