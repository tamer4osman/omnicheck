// Resolver corpus B (SPRINT-23): the submodule spelling -- the FIRST
// segment is the module (the recorded v1 boundary): `import Lib.Sub`
// resolves to module Lib's file exactly like the plain claim.
import Lib.Sub

func subUser() -> Int {
    return gamma()
}
