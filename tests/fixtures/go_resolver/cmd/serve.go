// SPRINT-10 package_clause entry-point fixture: this file is NOT named
// main.go, so the pack glob never claims it -- only the parsed
// package_clause ("main") does. OC006 must stay silent here, which is
// how the acceptance gate proves parse-fact claiming.
package main

// Serve is a tiny exported stub so the file also carries package
// surface. The main package is never imported, so OC005's own
// precondition keeps it silent (no verdict, not a wrong verdict).
func Serve() bool { return true }
