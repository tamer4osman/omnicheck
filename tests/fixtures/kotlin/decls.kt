// The OC005/symbol boundary fixture (SPRINT-22): the body-less abstract
// and interface members are NOT symbols (the C prototype discipline --
// Kotlin has no prototypes, every real definition carries a body); the
// interface default method IS a symbol; the private top-level helper is
// a symbol but never module surface (file-private, the C static rule's
// Kotlin spelling); the typealias and the enum class are surface and
// the enum ENTRIES are not (the documented boundary).
package app.shapes

abstract class Base {
    abstract fun run(): Int
    fun ready(): Int {
        return 1
    }
}

interface Repo {
    fun fetch(): Int
    fun default(): Int {
        return 1
    }
}

private fun hiddenHelper(): Int {
    return 7
}

typealias Handler = (Int) -> Unit

enum class Color {
    RED,
    GREEN
}
