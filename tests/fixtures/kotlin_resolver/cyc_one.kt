// Resolver corpus B (SPRINT-22): the import cycle, side one -- one.kt
// claims Two from the same package (the declared-fact single import
// works intra-package too: the declared package is app.cyc and Two is
// declared in cyc_two.kt); OC003 pins the cycle.
package app.cyc

import app.cyc.Two

class One {
    val pair = Two()
}
