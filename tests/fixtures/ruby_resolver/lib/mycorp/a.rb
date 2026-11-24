# frozen_string_literal: true

# The require cycle, leg one: the bare require hits the lib load root
# (lib/mycorp/b.rb).
require 'mycorp/b'

class CycleA
  def ping
    CycleB.new.pong
  end
end
