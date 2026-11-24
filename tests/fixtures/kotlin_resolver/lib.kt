// Resolver corpus B (SPRINT-22): the single-type import -- Engine is
// claimed from package app.core (core.kt); gamma is a top-level
// declaration the alias file imports by name.
package app.lib

import app.core.Engine

fun gamma(): Int {
    return Engine().start()
}
