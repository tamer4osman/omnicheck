// Resolver corpus B (SPRINT-22): the import cycle, side two -- two.kt
// claims One back (the cycle closes; OC003 == 1 on this corpus).
package app.cyc

import app.cyc.One

class Two {
    val mate = One()
}
