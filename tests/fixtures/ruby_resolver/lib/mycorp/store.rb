# frozen_string_literal: true

# Required by lib/mycorp.rb through the bare require (the <root>/lib
# load-root convention). Its require_relative child resolves to
# lib/mycorp/store/types.rb.
require_relative 'store/types'

class Store
  def self.open
    Types.load
  end
end
