"""Compat fixture v2safe: only additions (SAFE)."""


def public_fn(a):
    return a


def going_away(b):
    return b


def extra_fn(c):
    return c


__all__ = ["public_fn", "going_away", "extra_fn"]
