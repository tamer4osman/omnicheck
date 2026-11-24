# frozen_string_literal: true

# The require cycle, leg two: require_relative (the sibling channel).
# require cycles are LEGAL Ruby at runtime ($LOADED_FEATURES dedupes the
# second load); statically the cycle is a real import shape and OC003 is
# the honest finding (docs/LANGUAGES.md Ruby section 5).
require_relative 'a'

class CycleB
  def pong
    CycleA.new.ping
  end
end
