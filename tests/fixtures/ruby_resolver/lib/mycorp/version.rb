# frozen_string_literal: true

# Required by lib/mycorp.rb through require_relative (the sibling-child
# channel).
module MyCorp
  module Version
    STRING = '1.0.0'

    def self.current
      STRING
    end
  end
end
