// The OC004 rollup fixture (SPRINT-23): the hand-counted decision
// inventory of docs/LANGUAGES.md Swift section 4 -- every decision kind
// in one function, counted by construction and pinned by
// test_sprint23: comp(rollup) = 1 + 13 (if + else-if nested if + guard
// + ternary + two switch arms -- every arm counts including default --
// + for + while + repeat-while + one catch handler + the
// nil-coalescing + && + ||).
func rollup(n: Int, items: [Int]) -> Int {
    if n > 0 {
        return 1
    } else if n < 0 {
        return -1
    }
    guard !items.isEmpty else { return 0 }
    let e: Int? = nil
    var t = e ?? n
    t = t > 10 ? t : -t
    switch t {
    case 1:
        t += 10
    default:
        t += 2
    }
    for v in items {
        t += v
    }
    var i = 0
    while i < 2 {
        i += 1
    }
    repeat {
        t += 1
    } while t < -100
    do {
        t = try boomer(t)
    } catch {
        t = -1
    }
    let ok = t > 0 && items.count > 1
    let alt = ok || t > 500
    _ = alt
    return t
}
