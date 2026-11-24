// Resolver corpus B (SPRINT-23): module Lib carries exactly ONE file
// with exactly ONE top-level declaration -- the kind-scoped member
// claim `import func Lib.gamma` marks it by name (OC005 stays empty by
// construction: every export here IS the claimed name).
func gamma() -> Int {
    return 4
}
