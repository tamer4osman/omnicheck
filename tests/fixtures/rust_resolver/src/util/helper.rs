// The child module of util.rs (src/util.rs -> src/util/helper.rs). Its
// own use names an EXTERNAL crate path (serde) -- unresolved, honestly.
use serde::fix::Thing;

pub fn fmt_inner() -> String {
    let _t = Thing;
    format!("inner")
}
