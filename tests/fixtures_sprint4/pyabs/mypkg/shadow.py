"""Stdlib-named absolute imports stay unresolved (no intra-repo json.py)."""
import json


def dump():
    return json.dumps({"k": 1})
