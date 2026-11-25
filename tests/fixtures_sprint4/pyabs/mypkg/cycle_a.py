"""Cycle member A: absolute submodule import opens an OC003 cycle."""
import mypkg.cycle_b


def a():
    return mypkg.cycle_b.b()
