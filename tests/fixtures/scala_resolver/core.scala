// The resolver fixture (SPRINT-24): the declared package and the
// top-level names the importers claim.
package com.example.lib

class Service {
  def run(): Int = 1
}

def core(n: Int): Int = n + 1
