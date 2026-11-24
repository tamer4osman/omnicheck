// One half of the a/b use-cycle: legal Rust, one OC003 SCC.
use crate::b::B;

pub fn A() -> i64 {
    B()
}
