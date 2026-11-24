// The resolver fixture (SPRINT-24): the member import's SECOND
// declared reading -- package com.example.lib.Store would make total a
// top-level name in that package (the v1-kotlin declared-prefix walk
// matches BOTH readings honestly).
package com.example.lib.Store

def total(a: Int, b: Int): Int = a * b
