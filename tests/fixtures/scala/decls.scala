// The OC005/symbol boundary fixture (SPRINT-24): the body-less trait
// members are NOT symbols (the C prototype discipline); the trait
// default method IS a symbol; the private top-level helper is a symbol
// but never module surface (package-private, the C static rule's
// Kotlin spelling); the type alias and the Scala 3 enum are surface
// and the enum CASES are not (the documented boundary).
package app.shapes

trait Sourced {
  def name: String
  def source(): String = {
    name
  }
}

enum Color {
  case Red, Green, Blue
}

type Handler = Int => Unit

private def hidden(): Int = 1

def visible(n: Int): Int = n + 1

@main def entry(): Unit = {
  val s = Sourced
  println(visible(1))
}
