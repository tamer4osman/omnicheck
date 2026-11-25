// SPRINT-10 resolver fixture: the aliased import (st "util") binds the
// same package path; resolution is alias-independent.
package util

// U is the package's single exported function.
func U() string { return "util" }
