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
