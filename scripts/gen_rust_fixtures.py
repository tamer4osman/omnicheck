#!/usr/bin/env python3
"""Writes the SPRINT-15 Rust fixture corpora (structural + resolver + sprint15 unit fixtures).

Run once by hand; the fixtures are committed bytes (the gen_fuzz_corpus
convention -- tests never regenerate fixtures)."""
import os

# REPO was the checkout root when the fixtures were generated; the
# fixtures themselves are COMMITTED bytes (tests never regenerate).
REPO = "."

def write(path, content):
    full = os.path.join(REPO, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", newline="\n") as fh:
        fh.write(content)
    print("wrote", path, len(content.splitlines()), "lines")

# ------------------------------------------------------------- corpus A
# tests/fixtures/rust -- the structural corpus (5 .rs files), the Rust
# analogue of tests/fixtures/go. Zero resolving imports (std only), one
# comp-35 long function (OC001+OC004), one TODO (OC002), one duplicate
# block pair (OC008: 2 locations), one credential literal (OC011),
# entry claimed by the lib.rs glob (no OC006 there), the other four
# files OC006.

LIB_RS = '''\
// Spike entry point fixture (SPRINT-15, Track D wave 2): the pack
// entry-point globs (lib.rs / main.rs) claim this file -- the crate-root
// library target of the Rust-pack structural corpus.
//
// The only imports are std paths: NOTHING here names a module inside the
// analyzed set, so `oc graph` must show ZERO imports edges -- an edge
// always means "the target is part of this analysis", never a guess
// (docs/LANGUAGES.md Rust section 5). Positive resolution is pinned by
// tests/fixtures/rust_resolver.
use std::collections::HashMap;
use std::fmt::Write as _;

// An inline module: its items are NOT module surface (the source_file
// anchor keeps them out) but nested functions are still symbols with the
// same span contract.
pub mod internals {
    pub fn nested_add(a: i64, b: i64) -> i64 {
        a + b
    }
}

pub struct Greeter {
    name: String,
}

impl Greeter {
    // A method is a symbol with the same span contract, but NOT module
    // surface (the Go method rule; docs/LANGUAGES.md Rust section 6).
    pub fn hello(&self, who: &str) -> String {
        if who.is_empty() {
            return "hi".to_string();
        }
        format!("hi {}, from {}", who, self.name)
    }
}

// Greet is exported module surface (plain `pub`).
pub fn greet(who: &str) -> String {
    format!("hello {}", who)
}

// helper_private has NO visibility modifier: private, never surface.
fn helper_private(who: &str) -> String {
    format!("hey {}", who)
}

// Entry exercises the std imports and nothing else; both std paths point
// outside the analyzed set and stay unresolved, honestly.
pub fn entry(s: &str) -> String {
    let mut m: HashMap<String, String> = HashMap::new();
    m.insert("k".to_string(), s.to_string());
    let mut out = String::new();
    let _ = write!(out, "{}", m["k"]);
    out
}
'''

UTIL_RS_HEADER = '''\
// The Rust-pack spike fixtures (SPRINT-15, Track D wave 1 contract:
// docs/LANGUAGES.md Rust section).
//
// TODO: replace the ad-hoc dispatcher below with a table-driven loader.
//
// RustLongFunction spans more than the OC001 line threshold and carries
// more than the OC004 complexity threshold -- both rules must fire here,
// and their fingerprints must be stable across runs (acceptance gate).
// Its decision inventory is the COMPLEXITY CONTRACT itself (docs/
// LANGUAGES.md Rust section 4): 1 + 34 = complexity 35, one decision per
// recorded kind, hand-counted and pinned by the unit suite.

/// The decision-kind table, as code (every recorded kind appears).
pub fn rust_long_function(mode: i64, items: &[String]) -> String {
    let mut b = String::new();
    match mode {
        0 => b.push_str("zero"),
        1 => b.push_str("one"),
        _ => b.push_str("many"),
    }
    for it in items {
        if it.len() > 3 && it.starts_with('a') {
            b.push_str(it);
        } else if it.len() > 2 || it.contains('z') {
            b.push_str(&it.to_uppercase());
        }
        if items.len() % 2 == 0 && *mode != 2 {
            b.push_str("-even");
        }
        for _ in 0..(items.len() % 3) {
            b.push('.');
        }
    }
    let mut guard = 0;
    loop {
        guard += 1;
        if guard > 3 {
            break;
        }
    }
    let mut n = items.len();
    while n > 0 {
        n -= 1;
        b.push('-');
    }
    let r: Result<i64, String> = Ok(1);
    while let Some(_first) = items.first() {
        break;
    }
    if let Ok(_v) = r {
        b.push_str("ok");
    }
    match items.first().map(|s| s.as_str()) {
        Some("x") => b.push_str("x"),
        Some("y") | Some("z") => b.push_str("yz"),
        Some(_other) => b.push_str("other"),
        None => b.push_str("none"),
    }
    if r.is_ok() && *mode > 0 {
        b.push('?');
    }
    let _try = r.clone();
'''

# 6 trailing two-decision ifs to land exactly 34 decisions.
UTIL_RS_TAIL = '''\
    if *mode > 0 && items.len() > 0 {
        b.push_str("first");
    }
    if *mode > 1 && items.len() > 1 {
        b.push_str("second");
    }
    if *mode > 2 && items.len() > 2 {
        b.push_str("third");
    }
    if *mode > 3 || items.len() > 3 {
        b.push_str("fourth");
    }
    if *mode > 4 || items.len() > 4 {
        b.push_str("fifth");
    }
    if *mode > 5 && items.len() > 5 {
        b.push_str("sixth");
    }
    b
}

// Greet util is exported module surface used by lib.rs in real crates;
// in this corpus nothing resolves to it (std-only imports above).
pub fn greet_util(who: &str) -> String {
    format!("util hello {}", who)
}

// unexported_helper is private -- must NOT be an export candidate (the
// exportedness rule is the plain-`pub` rule; docs/LANGUAGES.md Rust
// section 6).
fn unexported_helper(who: &str) -> String {
    format!("util hey {}", who)
}
'''

DUP_BODY = '''\
// The OC008 fixture: the block below is duplicated VERBATIM in dup_a.rs
// and dup_b.rs (one group, two locations; the normalized 10+ line block
// is the duplicate-code unit). Keep it decision-free and comment-free
// beyond this header so exactly the duplicate rule fires.
pub fn dup_block(alpha: i64, beta: i64) -> i64 {
    let mut acc = alpha;
    acc += beta;
    acc *= 2;
    acc -= 1;
    if acc < 0 {
        acc = 0;
    }
    let scaled = acc / 3;
    let shifted = scaled << 2;
    shifted | (acc & 0xF)
}
'''

SECRET_RS = '''\
// Package cfg holds the OC011 fixture: a credential-shaped literal.
//
// AWS_ACCESS_KEY_ID holds AWS's documented example key id so OC011 fires
// deterministically in the adoption gate: content-based supply rules work
// the day a language registers. (The literal appears exactly ONCE in this
// file -- the sibling-identity contract folds byte-identical matches, and
// the gate asserts one finding.)
pub const AWS_ACCESS_KEY_ID: &str = "AKIAIOSFODNN7EXAMPLE";
'''

write("tests/fixtures/rust/lib.rs", LIB_RS)
write("tests/fixtures/rust/util/util.rs", UTIL_RS_HEADER + UTIL_RS_TAIL)
write("tests/fixtures/rust/util/dup_a.rs", DUP_BODY)
write("tests/fixtures/rust/util/dup_b.rs", DUP_BODY)
write("tests/fixtures/rust/cfg/secret.rs", SECRET_RS)

# ------------------------------------------------------------- corpus B
# tests/fixtures/rust_resolver -- the resolver corpus (11 .rs files +
# the decoy Cargo.toml). Pins every v1-rust reading; see the gate.

RESOLVER = {
"tests/fixtures/rust_resolver/Cargo.toml": '''\
# The decoy manifest: the RESOLVER never reads it (the go.mod
# discipline). It declares the crate name "mycorp" and tests/intg.rs
# imports `mycorp::...` while the crate is RIGHT HERE -- stripping the
# crate name would require the manifest, so the specifier stays
# unresolved (the Go `mycorp/store` precedent). The supply tier DOES
# read it: no licence field -> OC010 fires; the caret `serde = "1.0"`
# is NOT a pin -> OC009 warns (Cargo's own semantics; never guessed).
[package]
name = "mycorp"
version = "0.1.0"
edition = "2021"

[dependencies]
serde = "1.0"
''',
"tests/fixtures/rust_resolver/src/lib.rs": '''\
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
''',
"tests/fixtures/rust_resolver/src/a.rs": '''\
// One half of the a/b use-cycle: legal Rust, one OC003 SCC.
use crate::b::B;

pub fn A() -> i64 {
    B()
}
''',
"tests/fixtures/rust_resolver/src/b.rs": '''\
// The back edge of the a/b use-cycle.
use crate::a::A;

pub fn B() -> i64 {
    A()
}
''',
"tests/fixtures/rust_resolver/src/lonely.rs": '''\
// Declared by lib.rs (`mod lonely;`) -- in-degree 1, so NOT OC006
// material despite the name: a mod declaration is a real reference.
pub fn isolated() -> i64 {
    7
}
''',
"tests/fixtures/rust_resolver/src/orphan.rs": '''\
// Orphan: NO mod declaration anywhere names this file, it is not a
// target root and not test data -- in-degree 0, unclaimed: OC006 fires
// (cargo would never even compile it; the analysis reports what it
// sees).
pub fn uncompiled() -> i64 {
    13
}
''',
"tests/fixtures/rust_resolver/src/store/mod.rs": '''\
// A directory module: children live in src/store/, the module file is
// src/store/mod.rs. Pins the super:: climb (use super::a::A) and the
// brace-group self identity leaf (pub use self::types::Widget).
mod types;

use super::a::A;
pub use self::types::Widget;

pub fn store_root() -> i64 {
    A() + Widget::new().id()
}
''',
"tests/fixtures/rust_resolver/src/store/types.rs": '''\
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
''',
"tests/fixtures/rust_resolver/src/util.rs": '''\
// util.rs declares its own child module (the stem/ directory rule) and
// crosses into b through crate:: -- a second edge into the cycle files.
mod helper;

use self::helper::fmt_name;
use crate::b::B;

pub fn fmt_name() -> String {
    format!("util-{}", B())
}
''',
"tests/fixtures/rust_resolver/src/util/helper.rs": '''\
// The child module of util.rs (src/util.rs -> src/util/helper.rs). Its
// own use names an EXTERNAL crate path (serde) -- unresolved, honestly.
use serde::fix::Thing;

pub fn fmt_inner() -> String {
    let _t = Thing;
    format!("inner")
}
''',
"tests/fixtures/rust_resolver/src/bin/tool.rs": '''\
// A cargo bin target root: src/bin/*.rs are their OWN crates, so
// `crate::` here maps to THIS directory -- the lib crate's store module
// is a DIFFERENT crate and must NOT resolve (never guessed). The
// top-level fn main fact claims the file as an entry point.
use crate::store::types::Widget;

fn main() {
    let _w = Widget::new();
}
''',
"tests/fixtures/rust_resolver/tests/intg.rs": '''\
// Integration test data: a .rs file directly inside tests/ is runner
// data (cargo compiles each as its own crate) -- a zero in-degree is
// expected and never OC006 material. The `mycorp::` head names the
// SIBLING crate: external-crate claim, unresolved (the manifest is
// never read -- the decoy above declares it, and the resolver must not
// care).
use mycorp::store::types::Widget;

#[test]
fn widget_roundtrip() {
    let _w = Widget::new();
}
''',
}
for path, content in RESOLVER.items():
    write(path, content)

print("fixtures done")
