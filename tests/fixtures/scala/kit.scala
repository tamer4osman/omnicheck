// The alias-import target (SPRINT-24): `import com.example.kit.Helper as H2`
// resolves HERE -- same simple name as the tools object, different
// declared package: the declared-fact matching keys on the package
// clause, never on the name alone.
package com.example.kit

object Helper {
  val tag: String = "kit"
}
