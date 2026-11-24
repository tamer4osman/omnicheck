"""Suite for the mutation demo: kills every executed mutant except
the arg-swap survivor on pair_sum's add call (order-insensitivity is
untested -- the surviving mutant is the weak-test signal)."""
from calc import add, clamp, pair_sum, scale

assert add(2, 3) == 5
assert add(-1, 1) == 0
assert clamp(5, 0, 3) == 3
assert clamp(-2, 0, 3) == 0
assert clamp(2, 0, 3) == 2
assert clamp(2, 3, 1) == 3
assert scale(3, 2) == 6
assert scale(-1, 4) == -4
assert pair_sum(1, 2) == 3
assert pair_sum(-5, 5) == 0
print("suite ok")
