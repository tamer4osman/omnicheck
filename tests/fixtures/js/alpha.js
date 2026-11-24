// Fixture for CLI integration tests. At --config OC001.max_lines=5 the
// tooLong function trips OC001, and the markers trip OC002.

// TODO: split this module once the report format stabilizes.
function tooLong(a, b, c) {
  const sum = a + b + c;
  const doubled = sum * 2;
  const squared = doubled * doubled;
  const shifted = squared >> 2;
  const masked = shifted & 0xffff;
  const blended = masked | (squared & 0xf0000);
  const rotated = (blended << 3) | (blended >> 13);
  const mixed = rotated ^ doubled;
  return mixed >>> 0;
}

function fine(x) {
  return x + 1;
}

// FIXME: tooLong mixes concerns; extract step functions.
module.exports = { tooLong, fine };
