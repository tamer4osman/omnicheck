"""Mutation demo project: tiny calc with a deliberately weak suite."""


def add(a, b):
    return a + b


def clamp(n, lo, hi):
    if n < lo:
        return lo
    if n > hi:
        return hi
    return n


def scale(v, factor):
    v = v * factor
    return v


def pair_sum(a, b):
    # call site: the arg-swap mutant add(b, a) is order-insensitive and
    # the suite never checks order, so it survives by design.
    return add(a, b)
