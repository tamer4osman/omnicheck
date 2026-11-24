// SPRINT-15 unit fixture: every contracted symbol/comment/import shape
// in one file. The complexity contract is unit-pinned here (docs/
// LANGUAGES.md Rust section 4).
use crate::shapes::helper_mod;
use crate::shapes::{alpha, beta::gamma};
use std::collections::{HashMap, HashSet};
use ext::path::{inner1, inner2::*};
use crate::globbed::*;
use crate::aliased::thing as renamed;
use serde::external;

pub mod helper_mod;

/// Doc comment on the promoted const closure (counted as a plain
/// comment; the doc_comment child is part of the comment text).
const HOP: fn(i64) -> i64 = |x| if x > 0 { x } else { -x };

static LEAP: fn(i64) -> i64 = |x| match x {
    0 => 1,
    _ => 2,
};

static PLAIN: i64 = 7;

pub trait Swims {
    fn required(&self) -> i64;
    fn provided(&self) -> i64 {
        self.required() + 1
    }
}

extern "C" {
    fn foreign(a: i64) -> i64;
}

macro_rules! shard {
    ($x:expr) => {
        $x
    };
}

impl Swims for i64 {
    fn required(&self) -> i64 {
        // one of each decision kind + both operator tokens:
        // if, while, for, loop, match arm, try_expression, &&, ||
        let v = *self;
        if v > 0 && v < 100 {
            let mut i = 0;
            while i < v || i == 0 {
                i += 1;
            }
            for _ in 0..i {
                let _q: Result<i64, String> = Ok(1);
                let _r = _q?;
            }
            loop {
                break;
            }
        }
        match v {
            1 => 10,
            _ => 20,
        }
    }
}

pub fn alpha() -> i64 {
    // a LOCAL closure: decisions belong to THIS function (inclusive);
    // the binding is never a symbol.
    let f = |u: i64| if u > 0 { 1 } else { 2 };
    f(3)
}

pub(crate) fn crate_only() -> i64 {
    1
}

fn fully_private() -> i64 {
    2
}

pub struct Recorded {
    pub field: i64,
}

pub enum Choices {
    Yes,
    No,
}

pub type Alias = i64;

pub const LIMIT: i64 = 9;

pub static STATIC_LIMIT: i64 = 10;
