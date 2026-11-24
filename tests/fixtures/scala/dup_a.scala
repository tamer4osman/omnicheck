// The OC002 duplicate fixture (SPRINT-24): the SAME function body
// appears in dup_a.scala and dup_b.scala -- one clone pair, counted by
// construction.
package app.dup

def cloneTarget(n: Int): Int = {
  var acc = 0
  for (i <- 0 until n) {
    acc += i * 2
  }
  if (acc > 100) {
    acc - 1
  } else {
    acc + 1
  }
}
