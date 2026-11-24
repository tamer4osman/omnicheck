// The back edge of the a/b use-cycle.
use crate::a::A;

pub fn B() -> i64 {
    A()
}
