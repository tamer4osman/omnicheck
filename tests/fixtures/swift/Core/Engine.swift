// The Core module (SPRINT-23): intra-module files need NO import, so
// nobody on corpus A claims them -- the OC006 set carries both Core
// files (the module model made visible).
class Engine {
    func start() -> Int {
        return 1
    }
}

func kickstart() -> Int {
    return Engine().start()
}
