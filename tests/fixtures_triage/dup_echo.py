#!/usr/bin/env python3
"""Triage case-8 fixture: duplicate fingerprints and duplicate ids.

Echoes every request item once (rank = request position) and then
repeats the FIRST id with a different rank/confidence. The request the
test builds contains two findings that share one fingerprint (the
deliberate byte-identical-siblings case), so the response carries that
id twice PLUS the repeated first id.

The seam must: annotate every finding carrying a returned id (both
siblings), let the last entry for an id win (deterministic), and count
ranked_count as distinct findings, never as response entries.
"""
import json
import sys


def main() -> int:
    raw = sys.stdin.readline()
    if not raw.strip():
        print("{}", end="")
        return 0
    req = json.loads(raw)
    items = req.get("items", [])
    ranks = [
        {"id": it["id"], "rank": i, "confidence": 0.5}
        for i, it in enumerate(items)
    ]
    if items:
        ranks.append({"id": items[0]["id"], "rank": 7, "confidence": 0.25})
    print(json.dumps({"model": "dup-mock/0", "ranks": ranks}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
