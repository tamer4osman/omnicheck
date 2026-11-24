'use strict';

// Nothing here trips OC001 or OC002 at any threshold used by the tests.
function keepSmall(x) {
  return x + 1;
}

function alsoSmall(a, b) {
  return a * b;
}

module.exports = { keepSmall, alsoSmall };
