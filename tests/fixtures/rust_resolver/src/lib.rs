// The crate root: the mod tree is the module-tree import channel, the
// `use crate::` paths are the item-reading ladder (docs/LANGUAGES.md
// Rust section 5).
mod a;
mod b;
mod store;
mod util;
mod lonely;

use crate::a::A;
use crate::store::types::Widget;

pub fn root() -> i64 {
    A() + Widget::new().id()
}
