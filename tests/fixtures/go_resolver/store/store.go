// SPRINT-10 resolver fixture: package store lives in the directory the
// specifier "store" names under the analysis root. Two source files and
// one test file make the whole-package fan-out observable (three edges).
package store

// Name returns the package name.
func Name() string { return "store" }

// helper is unexported: not package surface, never an export candidate.
func helper() int { return 1 }
