class Sig
  def arity(a, b)
    a + b
  end

  def widened(a, b = 2)
    a + b
  end

  def reorder(a, b)
    [a, b]
  end

  def splat(a, *rest)
    a
  end
end
