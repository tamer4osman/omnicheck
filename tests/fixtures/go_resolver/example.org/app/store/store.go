// SPRINT-10 verbatim-mapping fixture: the dotted host "example.org" is
// a LITERAL directory name under the analysis root (this layout IS the
// GOPATH-src view, with the root as src/), and the import in
// cmd/main.go is the full path -- dots preserved, exactly Go's own
// mapping. A Python-style dot-to-slash conversion would destroy
// "example.org" and resolve nothing (the contract records the
// amendment, §Go.5).
package store

// Greeting is the package's exported function; the fan-out from
// cmd/main.go lands here and only here.
func Greeting() string { return "hello from the dotted-host tree" }
