// Resolver corpus B (SPRINT-24): the import cycle, side two -- claims
// One back (the cycle closes; OC003 == 1 on this corpus).
package com.example.cyc

import com.example.cyc.One

class Two {
  val mate = new One()
}
