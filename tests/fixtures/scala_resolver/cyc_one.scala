// Resolver corpus B (SPRINT-24): the import cycle, side one -- claims
// Two from the same package (the declared-fact single import works
// intra-package too: the declared package is com.example.cyc and Two
// is declared in cyc_two.scala); OC003 pins the cycle.
package com.example.cyc

import com.example.cyc.Two

class One {
  val mate = new Two()
}
