// The OC008 duplicate fixture b (SPRINT-23): the SAME function body
// appears in dup_a.swift and dup_b.swift -- one clone pair, counted by
// construction.
func dupTarget(x: Int) -> Int {
    let scaled = x * 3
    return scaled + 1
}

func dupCaller(seed: Int) -> Int {
    return dupTarget(seed) + dupTarget(seed + 1)
}
