"""Cycle member B: absolute submodule import closes the cycle."""
import mypkg.cycle_a


def b():
    return mypkg.cycle_a.a()
