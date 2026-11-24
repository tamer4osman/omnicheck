# Fixture for CLI integration tests. At --config OC001.max_lines=5 the
# too_long function trips OC001, and the markers trip OC002.

# TODO: move this pipeline behind a queue.
def too_long(a, b, c):
    total = a + b + c
    doubled = total * 2
    squared = doubled * doubled
    shifted = squared >> 2
    masked = shifted & 0xFFFF
    blended = masked | (squared & 0xF0000)
    rotated = ((blended << 3) | (blended >> 13)) & 0xFFFFFFFF
    mixed = rotated ^ doubled
    return mixed


def fine(x):
    return x + 1


# fixme: too_long mixes concerns; extract step functions.
if __name__ == "__main__":
    print(fine(too_long(1, 2, 3)))
