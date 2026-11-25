#!/usr/bin/env python3
"""Mock OmniDecide sidecar for protocol tests.

Speaks the v0 protocol documented in include/oc/triage.h: reads ONE JSON
line from stdin, responds with ONE JSON line on stdout. The production
shim will link omnidecide-c and speak the same protocol; this mock keeps
the client contract testable without that dependency (and its rank order
is deliberately reversed so tests can tell ranks apart).
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
    n = len(items)
    ranks = [
        {
            "id": it["id"],
            "rank": n - 1 - i,
            "confidence": round(0.9 - 0.05 * i, 3),
        }
        for i, it in enumerate(items)
    ]
    print(json.dumps({"model": "mock-omnidecide/0", "ranks": ranks}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
