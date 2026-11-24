// The single-import target (SPRINT-24): `import com.example.widgets.Widget`
// resolves HERE -- the declared package matches and this file declares a
// top-level Widget (the resolver's declared-fact contract).
package com.example.widgets

class Widget(val id: Int) {
  def scale(v: Int): Int = v * id
}
