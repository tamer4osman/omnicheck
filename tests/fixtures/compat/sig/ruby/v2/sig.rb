class Sig
  def arity(a, b, c)
    a + b + c
  end

  def widened(a, b = 2, c = 3)
    a + b + c
  end

  def reorder(b, a)
    [a, b]
  end

  def splat(a, b, *rest)
    a
  end
end
