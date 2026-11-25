// The type-shape fixture (SPRINT-23): generics, optionals, arrays,
// dictionaries, closures, variadics, inout, throws/async, external
// parameter labels -- the parameter extraction exercises every shape
// (docs/LANGUAGES.md Swift section 3).
func generic<T: Comparable>(_ a: T, _ b: T) -> T {
    return a
}

func labeled(external local: Int, rest: Int = 2) -> Int {
    return local + rest
}

func inoutp(v: inout Int) {
    v += 1
}

func variadic(items: Int..., tail: String) -> Int {
    return items.count
}

func throwing() throws -> Int {
    return 1
}

func loading() async throws -> Int {
    return 1
}

func closure(x: Int, f: (Int) -> Int) -> Int {
    return f(x)
}

func containers(m: [String: Int], a: [Int], o: Int?) -> Int {
    return a.count + (o ?? m.count)
}
