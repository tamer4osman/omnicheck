"""Deep module: package-root probing resolves absolute imports."""
import mypkg.sub

from mypkg.core import helper


def deep():
    return helper() + 1
