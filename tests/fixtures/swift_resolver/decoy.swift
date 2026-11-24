// Resolver corpus B (SPRINT-23): the unresolved decoys -- UIKit and
// Foundation are no declared target of this manifest (external
// modules), Mod.Missing names no target either: recorded and
// unresolved, never guessed. OC003/OC005 count nothing for them.
import UIKit
import Foundation
import Mod.Missing

func decoy() -> Int {
    return 0
}
