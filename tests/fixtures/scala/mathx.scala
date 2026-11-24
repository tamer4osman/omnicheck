// The comp-38 fixture (SPRINT-24): the hand-counted decision inventory
// -- 37 decisions, one per recorded kind (docs/LANGUAGES.md Scala
// section 4): twelve if_expression (five if/else pairs + four single
// ifs + the three boolean-guarded tail ifs), four while, two do-while,
// two for, twelve match arms across two matches (every arm counts --
// the Rust match_arm discipline), two catch arms, two && and one ||
// (the gated infix kind). The extent stays under the OC001.max_lines
// default so OC001 fires exactly once on this corpus -- the
// longfn.scala fixture.
package app.stats

object Mathx {
  def sum26(n: Int): Int = {
    var acc = 0
    if (n == 0) { acc = 1 } else { acc = 2 }
    if (n == 1) { acc += 1 } else { acc += 2 }
    if (n == 2) { acc += 1 } else { acc += 2 }
    if (n == 3) { acc += 1 } else { acc += 2 }
    if (n == 4) { acc += 1 } else { acc += 2 }
    if (n == 5) { acc += 1 }
    if (n == 6) { acc += 1 }
    if (n == 7) { acc += 1 }
    if (n == 8) { acc += 1 }
    var i = 0
    while (i < 4) { acc += 1; i += 1 }
    while (i < 5) { acc += 1; i += 1 }
    while (i < 6) { acc += 1; i += 1 }
    while (i < 7) { acc += 1; i += 1 }
    do { acc += 1; i += 1 } while (i < 9)
    do { acc += 1; i += 1 } while (i < 10)
    for (j <- 0 to 1) { acc += j }
    for (j <- 0 to 2) { acc += j }
    acc match {
      case 0 => acc = 1
      case 1 => acc = 2
      case 2 => acc = 3
      case 3 => acc = 4
      case 4 => acc = 5
      case _ => acc = 6
    }
    n match {
      case 0 => acc = 1
      case 1 => acc = 2
      case 2 => acc = 3
      case 3 => acc = 4
      case 4 => acc = 5
      case _ => acc = 6
    }
    try {
      acc = acc / n
    } catch {
      case ex: ArithmeticException => acc = -1
      case _ => acc = 0
    }
    if (acc > 0 && n > 0) { acc += 1 }
    if (acc < 0 || n < 0) { acc -= 1 }
    if (acc == 0 && n == 0) { acc = 1 }
    acc
  }
}
