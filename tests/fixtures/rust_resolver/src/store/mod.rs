// A directory module: children live in src/store/, the module file is
// src/store/mod.rs. Pins the super:: climb (use super::a::A) and the
// brace-group self identity leaf (pub use self::types::Widget).
mod types;

use super::a::A;
pub use self::types::Widget;

pub fn store_root() -> i64 {
    A() + Widget::new().id()
}
