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
