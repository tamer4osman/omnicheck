// Resolver corpus B (SPRINT-23): @testable import -- the attribute
// never changes the module: the whole-module claim fans out over BOTH
// Store files (the Kotlin wildcard discipline).
@testable import Store

func testUser() -> Int {
    return Engine().start() + alpha() + beta()
}
