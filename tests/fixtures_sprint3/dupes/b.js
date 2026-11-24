// Sprint 3 fixture: shares the 12-line normalized block with a.js --
// here it sits at different indentation (4 spaces, one tab line), which
// normalization must fold so the duplication is still found.

function gamma(seed) {
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
