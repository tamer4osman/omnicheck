// The whole-module import target (SPRINT-23): module Tools -- the NULL
// bind marks every export wholesale (importing a module loads all of
// it, the Kotlin wildcard discipline).
public enum Helper {
    case simple
    public static func crack(_ v: Int) -> Int {
        return v + 1
    }
}
