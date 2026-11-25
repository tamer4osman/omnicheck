# frozen_string_literal: true

# The runner-collected NAME convention (store_test.rb -- minitest): this
# file is runner data, never OC006 material. The `../..` traversal is
# legal Ruby and honest: the probe can only hit files inside the analyzed
# set (the requiring file lives at test/mycorp/, two levels above root).
require_relative '../../lib/mycorp'
require_relative '../../lib/mycorp/store'

class StoreTest < Minitest::Test
  def test_open
    assert_equal [], Store.open
  end
end
