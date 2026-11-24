// A cargo bin target root: src/bin/*.rs are their OWN crates, so
// `crate::` here maps to THIS directory -- the lib crate's store module
// is a DIFFERENT crate and must NOT resolve (never guessed). The
// top-level fn main fact claims the file as an entry point.
use crate::store::types::Widget;

fn main() {
    let _w = Widget::new();
}
