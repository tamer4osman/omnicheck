"""Compat fixture v1 (baseline)."""


def public_fn(a):
    return a


def going_away(b):
    return b


__all__ = ["public_fn", "going_away"]
