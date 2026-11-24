// Spike entry point fixture (SPRINT-9): the pack entry-point rule
// ("main.go") claims this file -- and since SPRINT-10 the parsed
// package_clause ("main") claims it too.
package main

import (
        "fmt"

        "example.com/app/internal/util"
)

// main imports one stdlib path and one module path; NEITHER names a
// package directory inside the analyzed fixture set (the util package
// lives at util/, not at example.com/app/internal/util), so both stay
// unresolved under resolver v1-go -- an edge always means "the target
// is part of this analysis", never a guess (docs/LANGUAGES.md §Go.5).
// Positive resolution is pinned by tests/fixtures/go_resolver.
func main() {
        fmt.Println(util.Greet("spike"))
}
