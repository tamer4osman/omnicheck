// The accessor + surface-boundary fixture (SPRINT-22): the getter with a
// real body IS a symbol (cached.get, the C# accessor precedent --
// accessor decisions previously counted toward NOTHING); class members
// are never module surface (the type-nesting precedent); the companion
// object constant and the nested class are members too.
package app.shapes

class Holder {
    val cached: Int
        get() {
            return 1
        }
    var tracked: Int = 0
        set(value) {
            field = if (value > 0) value else 0
        }

    companion object {
        const val KIND = "holder"
    }

    class Part(val name: String)
}
