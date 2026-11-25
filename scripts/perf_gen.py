#!/usr/bin/env python3
"""Generates the Sprint 1 performance-budget corpus: a deterministic
synthetic repository of 10,000 JavaScript files (200 dirs x 50 files,
~70 lines each) with functions, comments, and relative imports -- enough
to exercise discovery, parsing, both cheap passes, the cycle pass and
the complexity pass at M1-relevant scale. Output goes outside the repo
(work/perf10k) and is NOT committed."""
import os
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "work/perf10k"
DIRS = 200
FILES_PER_DIR = 50  # 10_000 files total

for d in range(DIRS):
    dirpath = os.path.join(ROOT, f"pkg{d:03d}")
    os.makedirs(dirpath, exist_ok=True)
    for f in range(FILES_PER_DIR):
        name = f"mod{f:02d}.js"
        parts = [
            f"// module {d}/{f} -- synthetic corpus, deterministic",
            f"const {{ helper{f} }} = require(\"./mod{(f + 1) % FILES_PER_DIR:02d}\");",
            "",
            "function worker" + str(f) + "(n) {",
            "  let acc = n;",
        ]
        for i in range(1, 12):
            parts.append(f"  if (acc % {i + 1} === 0) {{")
            parts.append(f"    acc += {i};")
            parts.append("  }")
        parts += [
            "  // TODO: replace loop with table",
            "  return acc;",
            "}",
            "",
            f"function entry{f}(m) {{",
            f"  return worker{f}(m) + helper{f}(m);",
            "}",
            "",
            f"module.exports = {{ entry{f} }};",
            "",
        ]
        with open(os.path.join(dirpath, name), "w") as fh:
            fh.write("\n".join(parts))

print(f"generated {DIRS * FILES_PER_DIR} files under {ROOT}")
