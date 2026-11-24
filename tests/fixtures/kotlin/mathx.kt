// The comp-35 fixture (SPRINT-22): the hand-counted decision
// inventory -- 34 decisions, one per recorded kind
// (docs/LANGUAGES.md Kotlin section 4): nine if/else-if pairs and
// two guard ifs (20 if_expression + 2 conjunction_expression), four
// while, two do-while, two for, one for-in, two elvis and one catch
// handler; every else arm and the default arm count as their own
// when_entry kind only. The definition extent stays under the
// OC001.max_lines default (60) so OC001 fires exactly once on this
// corpus -- the longfn.kt fixture.
package app.stats

fun sum26(n: Int): Int {
    if (n == 0) { n = 0 + 1 } else if (n == -0) { n = -0 - 1 }
    if (n == 1) { n = 1 + 1 } else if (n == -1) { n = -1 - 1 }
    if (n == 2) { n = 2 + 1 } else if (n == -2) { n = -2 - 1 }
    if (n == 3) { n = 3 + 1 } else if (n == -3) { n = -3 - 1 }
    if (n == 4) { n = 4 + 1 } else if (n == -4) { n = -4 - 1 }
    if (n == 5) { n = 5 + 1 } else if (n == -5) { n = -5 - 1 }
    if (n == 6) { n = 6 + 1 } else if (n == -6) { n = -6 - 1 }
    if (n == 7) { n = 7 + 1 } else if (n == -7) { n = -7 - 1 }
    if (n == 8) { n = 8 + 1 } else if (n == -8) { n = -8 - 1 }
    while (n > 100) { n-- }
    while (n > 100) { n-- }
    while (n > 100) { n-- }
    while (n > 100) { n-- }
    do { n++ } while (n < -100)
    do { n++ } while (n < -100)
    for (k0 in 0 until 2) { n += k0 }
    for (k1 in 0 until 2) { n += k1 }
    for (v in intArrayOf(1, 2)) { n += v }
    val opt: Int? = n
    n = opt ?: 1
    val opt2: Int? = n
    n = opt2 ?: 2
    try { n = n + 1 } catch (ex: ArithmeticException) { n = -1 }
    if (n > 0 && n < 10) { n = 10 }
    if (n > -10 && n < 0) { n = -10 }
    return n
}
