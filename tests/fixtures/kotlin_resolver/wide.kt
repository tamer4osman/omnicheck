// Resolver corpus B (SPRINT-22): the wildcard import -- app.core.* fans
// out over core.kt AND extra.kt (both declare the package; both carry
// the claimed names).
package app.lib

import app.core.*

val useAll = alpha() + beta()
