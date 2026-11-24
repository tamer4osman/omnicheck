// Resolver corpus B (SPRINT-23): the whole-module claim from a LOOSE
// file (module-less importers are legal: only the TARGET needs
// declared facts) -- `import Wide` fans out over BOTH Wide files.
import Wide

func wideUser() -> Int {
    return walpha() + wbeta()
}
