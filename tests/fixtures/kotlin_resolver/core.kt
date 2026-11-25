// Resolver corpus B (SPRINT-22): package app.core carries TWO files --
// the single-type import of Engine resolves to THIS file (the declared
// symbol Engine lives here), the wildcard fan-out reaches both.
package app.core

class Engine {
    fun start(): Int {
        return 1
    }
}

fun alpha(): Int {
    return 2
}
