// SPRINT-10 OC006 fixture: nothing imports this package and no entry
// rule claims it (not package main, not a test), so the file is
// reported as unreferenced -- structurally true, intent-blind; triage
// decides.
package lonely

// L is the package's exported constructor, never referenced in-run.
func L() int { return 0 }
