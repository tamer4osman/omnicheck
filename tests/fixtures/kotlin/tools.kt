// The wildcard-import target (SPRINT-22): `import com.example.tools.*`
// fans out over the package's files -- this file declares the package
// and the object Helper (the Java asterisk-marker fan-out contract).
package com.example.tools

object Helper {
    const val TAG = "tools"

    fun crack(n: Int): Int {
        return n + TAG.length
    }
}
