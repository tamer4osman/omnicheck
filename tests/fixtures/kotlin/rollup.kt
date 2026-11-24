// The OC004 rollup fixture (SPRINT-22): the hand-counted decision
// inventory of docs/LANGUAGES.md Kotlin section 4 -- every decision kind
// in one function, counted by construction and pinned by test_sprint22:
// comp(rollup) = 1 + 12 (if + else-if nested if_expression + two when
// arms -- every arm counts including else -- + for + for-in + while +
// do-while + one catch handler + the elvis + && + ||). Lambdas and
// anonymous functions are anonymous: their decisions count inward, the
// constructs are never symbols.
package app.stats

fun rollup(n: Int, items: List<Int>): Int {
    if (n > 0) {
        return 1
    } else if (n < 0) {
        return -1
    }
    when (n) {
        in 3..9 -> return 30
        else -> {}
    }
    for (v in items) {
        n += v
    }
    var i = 0
    for (k in 0 until 2) {
        i++
    }
    while (n > 100) {
        n--
    }
    do {
        n++
    } while (n < -100)
    var t = 0
    try {
        t = t + n
    } catch (ex: IllegalStateException) {
        t = -1
    }
    val e = n ?: -1
    val ok = n > 0 && e != 0
    return t + i + if (ok) 1 else 0 || n
}

fun one(): Int {
    return 1
}
