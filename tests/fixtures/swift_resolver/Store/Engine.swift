// Resolver corpus B (SPRINT-23): module Store carries TWO files -- the
// whole-module claim (@testable or plain) fans out over BOTH (the
// Kotlin wildcard discipline at module scale).
class Engine {
    func start() -> Int {
        return 1
    }
}

func alpha() -> Int {
    return 2
}
