// Sprint 3 fixture: shares a 12-line normalized block with b.js (which
// uses different indentation -- 4 spaces and one tab) and a 9-line block
// with short.js (below the default OC008.min_block of 10, so silent).

function alpha(seed) {
  const base = compute(seed);
  const offset = base % 7;
  if (offset < 2) {
    base += 10;
  } else if (offset < 4) {
    base -= 3;
  }
  for (let i = 0; i < offset; i++) {
    base += i * 2;
  }
  return finalize(base);
}

function beta() {
        const w1 = weight('a');
        const w2 = weight('b');
        const w3 = weight('c');
        if (w1 > w2) {
          w3 += 1;
        }
        if (w2 > w3) {
          w1 -= 1;
        }
        return norm(w1, w2, w3);
}
