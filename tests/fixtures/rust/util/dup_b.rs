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
