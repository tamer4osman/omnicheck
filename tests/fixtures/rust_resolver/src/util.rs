// util.rs declares its own child module (the stem/ directory rule) and
// crosses into b through crate:: -- a second edge into the cycle files.
mod helper;

use self::helper::fmt_name;
use crate::b::B;

pub fn fmt_name() -> String {
    format!("util-{}", B())
}
