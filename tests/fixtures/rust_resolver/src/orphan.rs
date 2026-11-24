// Orphan: NO mod declaration anywhere names this file, it is not a
// target root and not test data -- in-degree 0, unclaimed: OC006 fires
// (cargo would never even compile it; the analysis reports what it
// sees).
pub fn uncompiled() -> i64 {
    13
}
