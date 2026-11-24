// The kind-scoped import fixture (SPRINT-23): `import typealias Kit.Tag`
// names the member Tag from module Kit -- the named claim marks the
// matching export. The file has NO top-level statements (a struct is a
// declaration, not code) and no incoming claims: OC006 fires here.
import typealias Kit.Tag

struct Holder {
    var tag = Tag(id: 1)
    func show() -> String {
        return tag.describe()
    }
}
