// The entry fixture (SPRINT-23): the top-level statements are the
// entry-point parse fact (the Q_SW_TOP contract -- a source_file with
// direct executable statements). The kind-scoped struct import claims
// Widget from the Widgets module (the named bind); the whole-module
// import claims the Tools module wholesale (the NULL bind).
import struct Widgets.Widget
import Tools

let boot = Widget(id: 3)
let scaled = boot.scale(2)
print(Helper.crack(scaled))
