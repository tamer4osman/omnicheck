// Sprint 3 fixture: the 9-line block below also appears in a.js beta()
// but is BELOW the default OC008.min_block of 10, so OC008 must stay
// silent about it at the default configuration.

function gamma2() {
        const w1 = weight('a');
        const w2 = weight('b');
        const w3 = weight('c');
        if (w1 > w2) {
          w3 += 1;
        }
        if (w2 > w3) {
          w1 -= 1;
        }
        return scaled(norm(w1, w2, w3), 2);
}
