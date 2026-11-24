// SPRINT-10 OC003 fixture: package a imports package b, and b imports
// a back -- a two-package cycle that the file-level SCC sees as one
// cycle among two files.
package a

import "b"

// A calls into b, closing the cycle.
func A() int { return b.B() + 1 }
