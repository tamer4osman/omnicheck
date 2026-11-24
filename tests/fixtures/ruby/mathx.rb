# frozen_string_literal: true

require 'json'

# MathX is export surface (a top-level module). The comp-35 method is the
# hand-counted decision inventory of docs/LANGUAGES.md Ruby section 4 --
# 34 decisions, one per recorded kind (the Ruby analogue of the Rust
# comp-35: ONE decision per when clause, the comma patterns are ONE
# clause; an if_guard inside an in_clause does NOT count separately).
module MathX
  INVENTORY = [1, 2, 3].freeze

  # Decisions (34): if(1) + elsif(2) + unless(1) + while(1) + until(1)
  # + for(1) + when(4) + in_clause(3) + rescue(3) + rescue_modifier(1)
  # + if_modifier(2) + unless_modifier(1) + while_modifier(1)
  # + until_modifier(1) + conditional(1) = 24 kind decisions, plus
  # &&(6) + ||(2) + and(1) + or(1) = 10 operator decisions -> 34 total,
  # complexity 35.
  def inventory(x, ys)
    total = 0
    if x.positive? && ys.size > 0
      base = 1
    elsif x.zero? && ys.empty? || ys.nil?
      base = 2
    elsif x.negative?
      base = 3
    else
      base = 4
    end
    unless base == 4
      total += base
    end
    while x > 10
      x -= 10
    end
    until x <= 0 && total > 0
      x -= 1
    end
    for y in ys
      total += y
    end
    case x
    when 0 then total += 1
    when 1 then total += 2
    when 2, 3 then total += 3
    when Integer then total += 4
    else total += 5
    end
    case x
    in Integer => n then total += n
    in [a, b] if a then total += b
    in String then total += 1
    else total += 0
    end
    begin
      total /= x
    rescue ZeroDivisionError
      total = 0
    rescue TypeError, RangeError => e
      total = -1
    rescue StandardError
      total = -2
    ensure
      total += 0
    end
    fallback = total rescue 0
    fallback += 1 unless x.nil?
    fallback += 1 while x > 99
    fallback += 1 until x > 999
    fallback = x.positive? ? fallback : 0
    fallback += 1 if x.positive? && total.positive? && base.positive? && fallback >= 0
    fallback += 1 if ys.empty? || x.zero?
    fallback = 1 and fallback = 2
    fallback = 3 or fallback = 4
    fallback
  end

  # A bodyless (empty) method: a symbol with complexity exactly 1.
  def hook; end
end
