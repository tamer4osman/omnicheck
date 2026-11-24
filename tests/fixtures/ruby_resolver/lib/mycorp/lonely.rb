# frozen_string_literal: true

# Never required by anyone: OC006 fires here. The Gemfile DEPENDENCIES
# decoy naming the gem is never read BY THE RESOLVER (the manifest is
# supply-tier data, not load-path data).
class Lonely
  def wander
    2
  end
end
