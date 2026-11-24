// The single-type import target (SPRINT-23): module Widgets carries
// exactly ONE top-level declaration (struct Widget) -- the named bind
// claims it, so OC005 stays empty by construction. The accessor bodies
// ARE symbols (the C# SPRINT-14 precedent): the implicit getter
// (doubled.get), the explicit pair (tracked.get/tracked.set) and the
// observer (observed.willSet, comp 2 -- one if); the EMPTY didSet body
// matches nothing (the body-less accessor rule, pinned by its absence).
public struct Widget {
    public let id: Int
    private var cached: Int = 0
    public init(id: Int) {
        self.id = id
    }
    public func scale(_ v: Int) -> Int {
        return v * id
    }
    public var doubled: Int {
        return id * 2
    }
    public var tracked: Int {
        get {
            return cached
        }
        set(v) {
            cached = v
        }
    }
    public var observed: Int = 0 {
        willSet {
            if newValue > cached {
                cached = newValue
            }
        }
        didSet {
        }
    }
}
