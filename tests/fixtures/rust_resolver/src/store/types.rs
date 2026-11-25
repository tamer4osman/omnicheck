// The item-reading target of `use crate::store::types::Widget` (the
// full-module probe src/store/types/Widget.rs does not exist; the
// parent-module file does).
pub struct Widget {
    pub id: i64,
}

impl Widget {
    pub fn new() -> Self {
        Widget { id: 1 }
    }
    pub fn id(&self) -> i64 {
        self.id
    }
}
