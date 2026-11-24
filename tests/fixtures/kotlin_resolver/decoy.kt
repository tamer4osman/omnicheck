// Resolver corpus B (SPRINT-22): the unresolved decoys -- java.util.*
// (the two-segment prefix java matches NO declared package) and
// app.missing.Ghost (no such declared package) stay recorded and
// unresolved, never guessed. OC003/OC005 count nothing for them.
package app.decoy

import java.util.ArrayList
import app.missing.Ghost

fun decoy(): Int {
    return 0
}
