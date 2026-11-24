// Resolver corpus B (SPRINT-23): the default-path target claim --
// `import Std` resolves through the SPM documented default
// Sources/Std.
import Std

func stdUser() -> Int {
    return standalone()
}
