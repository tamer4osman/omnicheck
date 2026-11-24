// The comp-35 fixture (SPRINT-23): the hand-counted decision inventory
// -- 34 decisions, one per recorded kind (docs/LANGUAGES.md Swift
// section 4): 7 if/else-if pairs (14 if_statement, else-if nested), 3
// guard, 2 nil-coalescing, 1 ternary, 3 switch arms (every arm counts
// including default), 2 for, 4 while, 2 repeat-while, 1 catch handler,
// 1 conjunction and 1 disjunction. The definition extent stays under
// the OC001.max_lines default (60) so OC001 fires exactly once on this
// corpus -- the longfn.swift fixture.
func sum26(n: Int, items: [Int]) -> Int {
    var t = 0
    if n > 0 { t = 1 } else if n < 0 { t = -1 }
    if n == 1 { t = 2 } else if n == 2 { t = -2 }
    if n == 3 { t = 3 } else if n == 4 { t = -3 }
    if n == 5 { t = 4 } else if n == 6 { t = -4 }
    if n == 7 { t = 5 } else if n == 8 { t = -5 }
    if n == 9 { t = 6 } else if n == 10 { t = -6 }
    if n == 11 { t = 7 } else if n == 12 { t = -7 }
    guard items.count > 0 else { return 0 }
    guard items.count > 1 else { return 1 }
    guard items.count > 2 else { return 2 }
    let e: Int? = nil
    t = t + (e ?? 0)
    t = t + (e ?? 1)
    t = t > 100 ? t : -t
    switch n {
    case 0:
        t += 100
    default:
        t += 1
    }
    switch items.first {
    default:
        break
    }
    for v in items { t += v }
    for i in 0..<2 { t += i }
    var w = n
    while w > 100 { w -= 1 }
    while w < -100 { w += 1 }
    while w > 90 { w -= 1 }
    while w < -90 { w += 1 }
    repeat { w += 1 } while w < 0
    repeat { w -= 1 } while w > 0
    do {
        t = try boomer(t)
    } catch {
        t = 0
    }
    let ok = t > 0 && items.isEmpty
    let alt = ok || t > 1000
    _ = alt
    return t
}

func boomer(_ x: Int) throws -> Int {
    return x
}
