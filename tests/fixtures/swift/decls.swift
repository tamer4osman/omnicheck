// The structural surface fixture (SPRINT-23): every top-level
// declaration shape surfaces (functions, class/struct/enum/extension
// via the anonymous declaration_kind token, protocol, typealias,
// stored globals); members, enum entries and accessors never surface
// (the type-nesting precedent).
func freeFn(x: Int) -> Int {
    return x
}

class Klass {
    func member() -> Int {
        return 1
    }
}

struct St {
    let id = 0
}

enum Mode {
    case on
    case off
}

extension Int {
    func doubled() -> Int {
        return self * 2
    }
}

protocol Spec {
    func run() -> Int
}

typealias Handler = (Int) -> Int

let globalLet = 1
var globalVar = 2
