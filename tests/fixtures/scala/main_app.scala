// The entry fixture (SPRINT-24): the object-main definition is the
// entry-point parse fact (the Q_SC_MAIN contract -- a def named main
// with a parameters list inside an object); the single and the
// wildcard import make this file the hub -- Widget claims widget.scala,
// the wildcard claims the whole com.example.tools package.
package app.main

import com.example.widgets.Widget
import com.example.tools.*

object App {
  def main(args: Array[String]): Unit = {
    val w = Widget(3)
    val scaled = w.scale(2)
    val cracked = Helper.crack(scaled)
    println(cracked)
  }
}
