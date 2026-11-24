// Sprint 3 fixture: intra-file duplication -- the same 10-line block
// appears twice in this file. The lines before and after each occurrence
// differ, so lockstep extension must stop exactly at the block edges.

function one() {
  let acc = 0;
  acc += 1;
  acc += 2;
  acc += 3;
  acc += 4;
  acc += 5;
  acc += 6;
  acc += 7;
  acc += 8;
  acc += 9;
  return acc + 100;
}

function two(n) {
  let acc = 0;
  acc += 1;
  acc += 2;
  acc += 3;
  acc += 4;
  acc += 5;
  acc += 6;
  acc += 7;
  acc += 8;
  acc += 9;
  return acc + 200;
}
