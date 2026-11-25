// The wildcard-import target (SPRINT-24): `import com.example.tools.*`
// fans out over the package's files -- this file declares the package
// and the object Helper (the Java asterisk-marker fan-out contract).
package com.example.tools

object Helper {
  val TAG: String = "tools"

  def crack(n: Int): Int = {
    n + TAG.length
  }
}
