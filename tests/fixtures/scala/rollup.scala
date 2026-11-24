// The OC004 rollup fixture (SPRINT-24): the hand-counted decision
// inventory of docs/LANGUAGES.md Scala section 4 -- every decision kind
// in one function, counted by construction and pinned by
// test_sprint24: comp(rollup) = 1 + 13 (if + else-if nested
// if_expression + three match arms -- every arm counts including the
// wildcard -- + for + while + do-while + TWO catch arms -- the catch
// decision IS its case_block arms, no separate catch kind -- + && + ||
// + the trailing if, the infix kind gated to the boolean operators in
// C). Lambdas are anonymous: their decisions count inward, the
// construct is never a symbol.
package app.stats

object Roller {
  def rollup(n: Int, items: List[Int]): Int = {
    if (n > 0) {
      return 1
    } else if (n < 0) {
      return -1
    }
    n match {
      case 1 | 2 => return 20
      case k if k > 3 => return 30
      case _ => {}
    }
    for (v <- items) {
      n + v
    }
    var i = n
    while (i > 100) {
      i -= 1
    }
    do {
      i += 1
    } while (i < 3)
    try {
      i = i / n
    } catch {
      case ex: ArithmeticException => i = -1
      case _ => i = 0
    }
    val ok = n > 0 && items.nonEmpty || i == 0
    if (ok) i else 0
  }
  def one: Int = 1
}
