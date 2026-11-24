// The entry fixture (SPRINT-22): the top-level `fun main` definition is
// the entry-point parse fact (the Q_KT_MAIN contract); the single and
// the wildcard import make this file the hub -- Widget claims
// widget.kt, the wildcard claims the whole com.example.tools package.
package app.main

import com.example.widgets.Widget
import com.example.tools.*

fun main(args: Array<String>) {
    val w = Widget(3)
    val scaled = w.scale(2)
    val cracked = Helper.crack(scaled)
    println(cracked)
}
