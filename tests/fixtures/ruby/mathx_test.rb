# frozen_string_literal: true

require 'minitest/autorun'
require 'json'

# The runner-collected NAME convention (mathx_test.rb -- minitest): this
# file is runner data, a zero in-degree is expected and is never OC006
# material (docs/LANGUAGES.md Ruby section 6).
class MathXTest < Minitest::Test
  def test_inventory_smoke
    assert_kind_of Array, MathX::INVENTORY
  end
end
