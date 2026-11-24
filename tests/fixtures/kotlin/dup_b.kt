// The OC008 duplicate fixture b (SPRINT-22): the SAME function body
// appears in dup_a.kt and dup_b.kt -- one clone pair, counted by
// construction.
package app.dup

fun dupTarget(x: Int): Int {
    val scaled = x * 3
    return scaled + 1
}

fun dupCaller(seed: Int): Int {
    return dupTarget(seed) + dupTarget(seed + 1)
}
