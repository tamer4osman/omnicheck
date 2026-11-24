// The import-channels fixture (SPRINT-22): the alias import binds the
// ORIGINAL name (H2 is local surface detail); the member import
// (Helper.tag) is the ambiguous member shape -- the whole-file claim
// direction (NULL bind), both readings resolved against the declared
// facts.
package app.usage

import com.example.kit.Helper as H2
import com.example.kit.Helper.tag

val aliased = H2.tag
val memberUsed = tag
