// Resolver corpus B (SPRINT-22): the alias import binds the ORIGINAL
// name (Motor is local detail -- the export Engine is marked used) and
// the two-segment gamma import claims the app.lib declaration.
package app.util

import app.core.Engine as Motor
import app.lib.gamma

fun eps(): Int {
    return Motor().start() + gamma()
}
