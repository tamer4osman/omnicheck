// The resolver fixture (SPRINT-24): the member-import reading -- the
// object Store carries the member total (the a.b.C.f shape's first
// declared reading).
package com.example.lib

object Store {
  def total(a: Int, b: Int): Int = a + b
}
