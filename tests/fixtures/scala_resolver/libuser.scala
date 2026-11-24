// The resolver fixture (SPRINT-24): the import shapes -- single
// (com.example.lib.core), wildcard (com.example.lib.types.*), selector
// pair (TypeA, TypeB), the Scala 3 tail rename (Service as Svc) and
// the member claim (com.example.lib.Store.total -- the ambiguous
// member shape, NULL bind, both declared readings).
package app.use

import com.example.lib.core
import com.example.lib.types.*
import com.example.lib.types.{TypeA, TypeB}
import com.example.lib.Service as Svc
import com.example.lib.Store.total

def useAll(): Int = {
  val svc = Svc
  val a = TypeA
  val b = TypeB
  core(a.id) + svc.run() + total(1, 2)
}
