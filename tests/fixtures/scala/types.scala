// The surface-boundary + Scala 3 fixture (SPRINT-24): class members are
// never module surface (the type-nesting precedent); the companion
// object constant and the nested class are members too; the extension
// method IS a symbol (the body belongs to its def); the given with a
// body is a container, its compare method is the symbol; top-level
// val/var ARE surface (the simple pattern shape).
package app.shapes

class Holder {
  val cached: Int = {
    1
  }

  def hit(n: Int): Int = n

  object Registry {
    val kind: String = "holder"
  }

  class Part(val name: String)
}

extension (s: String) {
  def shout: String = s.toUpperCase
}

val topVal: Int = 7
var topVar: Int = 8
