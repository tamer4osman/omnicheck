// SPRINT-10 test-convention fixture: go test runs this file directly,
// so a zero in-degree is expected and never OC006 material (the
// *_test.go convention). The "testing" import is stdlib: unresolved.
// Fan-out note: _test.go files never join an import (Go compiles a
// package import without them, §Go.5), so no importer of "store"
// reaches this file -- the convention above is what keeps OC006
// silent here.
package store

import "testing"

// TestStore is a plain stub; exported by case, and marked used by the
// package-wide fail-open rule because the package is imported.
func TestStore(t *testing.T) {
	if Name() != "store" {
		t.FailNow()
	}
}
