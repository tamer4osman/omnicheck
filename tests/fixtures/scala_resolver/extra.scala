// Resolver corpus B (SPRINT-24): the SECOND file of package
// com.example.lib -- nothing claims THIS file (no import points at
// unclaimed; the file-level OC006 fires instead and the OC005 export
// pass stays empty BY CONSTRUCTION on this corpus -- the honest
// outcome, the Kotlin corpus B shape).
package com.example.lib

def unclaimed(n: Int): Int = n * 3
