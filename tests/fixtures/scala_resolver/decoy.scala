// The resolver fixture (SPRINT-24): the SAME simple names in a
// DIFFERENT declared package -- the declared-fact matching keys on the
// package clause, never on the name alone.
package com.example.other

class TypeA {
  def id: Int = 99
}

object Store {
  def total(a: Int, b: Int): Int = a - b
}
