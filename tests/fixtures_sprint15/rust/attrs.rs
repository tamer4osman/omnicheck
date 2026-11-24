// SPRINT-15 unit fixture: path-attributed mods are NOT probed by the
// resolver (the attribute moves the file; probing the conventional name
// could fabricate a WRONG edge), and pub use re-exports record the same
// import channel.
#[cfg(test)]
mod attr_moved;

#[path = "moved_elsewhere.rs"]
mod attr_moved;

mod real_child;

pub use self::real_child::thing;

pub mod real_child {
    pub fn thing() -> i64 {
        3
    }
}

pub fn drive(v: Option<i64>) -> i64 {
    while let Some(w) = v {
        return w;
    }
    0
}
