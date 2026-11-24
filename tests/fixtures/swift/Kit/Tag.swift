// The kind-scoped import target (SPRINT-23): module Kit carries exactly
// ONE top-level declaration (struct Tag) -- the named bind claims it.
public struct Tag {
    public let id: Int
    public init(id: Int) {
        self.id = id
    }
    public func describe() -> String {
        return "tag\(id)"
    }
}
