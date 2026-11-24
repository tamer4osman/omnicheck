// SPRINT-10 OC003 fixture: the back edge of the a/b package cycle.
package b

import "a"

// B calls back into a; the SCC spans both packages' files.
func B() int { return a.A() }
