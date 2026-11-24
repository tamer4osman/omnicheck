"""Compat fixture v2: going_away removed (BREAKING), new_export added."""


def public_fn(a):
    return a


def replacement_fn(b):
    return b


__all__ = ["public_fn", "replacement_fn"]
