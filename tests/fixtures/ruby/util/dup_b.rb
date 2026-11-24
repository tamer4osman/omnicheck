# frozen_string_literal: true

require 'json'

# The OC008 duplicate-code pair: the same 10-line normalized block in two
# files (one group, two locations -- the rust corpus convention).
class LedgerB
  def first_page(rows, audit, seen)
    total = 0
    rows.each do |r|
      next unless r.valid?
      total += r.value
      audit << r.tag
      seen[r.key] = true
    end
    total
  end

  def second_page(rows, audit, seen)
    total
  end
end
