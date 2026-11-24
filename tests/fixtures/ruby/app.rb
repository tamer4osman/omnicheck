# frozen_string_literal: true

# The structural corpus "app" file (SPRINT-16): symbols, the comp-10
# hand-count, the OC001 long method and the OC002 marker.
#
# Every require on this corpus names an external gem: NOTHING here names a
# file inside the analyzed set, so `oc graph` must show ZERO imports edges
# -- an edge always means "the target is part of this analysis", never a
# guess (docs/LANGUAGES.md Ruby section 5). Positive resolution is pinned
# by tests/fixtures/ruby_resolver.
require 'json'
require 'set'

# App is export surface (a top-level class).
class App
  # comp-10: the hand-counted decision inventory -- one decision per
  # recorded kind (docs/LANGUAGES.md Ruby section 4). Decisions: if(1),
  # elsif(1), while(1), for(1), when(2 -- one per clause, the
  # comma-separated patterns are ONE clause), ternary(1), &&(1) = 7 ->
  # complexity 8? NO: the inventory below is 9 decisions -> comp 10.
  def flow(x)
    if x > 0
      sign = 1
    elsif x == 0
      sign = 0
    else
      sign = -1
    end
    while x > 100
      x -= 100
    end
    for i in 0..2
      sign += i
    end
    case x
    when 0, 1 then tag = :low
    when Integer then tag = :num
    else tag = :other
    end
    z = sign.positive? ? 1 : 0
    z += 1 if x.positive? && sign.positive?
    z
  end

  # A singleton method is a symbol with the same span contract.
  def self.build(name)
    new(name)
  end

  # An endless definition is the same method node (Ruby 3.0+).
  def short_name = :endless

  # An empty method is a symbol with complexity exactly 1.
  def empty_hook; end

  # A setter keeps the name as written.
  def name=(v)
    @name = v
  end

  # An operator method keeps the name as written.
  def [](idx)
    idx
  end
end

# TODO: split the long method below once the report shape is settled.
def long_runner(seed)
  acc = seed
  acc += 1
  acc += 2
  acc += 3
  acc += 4
  acc += 5
  acc += 6
  acc += 7
  acc += 8
  acc += 9
  acc += 10
  acc += 11
  acc += 12
  acc += 13
  acc += 14
  acc += 15
  acc += 16
  acc += 17
  acc += 18
  acc += 19
  acc += 20
  acc += 21
  acc += 22
  acc += 23
  acc += 24
  acc += 25
  acc += 26
  acc += 27
  acc += 28
  acc += 29
  acc += 30
  acc += 31
  acc += 32
  acc += 33
  acc += 34
  acc += 35
  acc += 36
  acc += 37
  acc += 38
  acc += 39
  acc += 40
  acc += 41
  acc += 42
  acc += 43
  acc += 44
  acc += 45
  acc += 46
  acc += 47
  acc += 48
  acc += 49
  acc += 50
  acc += 51
  acc += 52
  acc += 53
  acc += 54
  acc += 55
  acc += 56
  acc += 57
  acc += 58
  acc += 59
  acc += 60
  acc += 61
  acc += 62
  acc
end
