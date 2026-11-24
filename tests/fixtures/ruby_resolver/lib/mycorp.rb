# frozen_string_literal: true

# The gem main file (the resolver corpus entry shape: required by the
# test through the `..` traversal). Edge pins:
#   require_relative 'mycorp/version' -> lib/mycorp/version.rb
#   require_relative 'mycorp/cli'     -> lib/mycorp/cli.rb
#   require 'mycorp/store'            -> lib/mycorp/store.rb (the bare
#                                        require hits the <root>/lib
#                                        load-root convention)
# The decoys below stay unresolved, honestly:
#   require 'json' / Kernel.require 'set'  -- external, $LOAD_PATH unknown
#   require 'mycorp/missing'               -- no such analyzed file
require_relative 'mycorp/version'
require_relative 'mycorp/cli'
require 'mycorp/store'
require 'json'
Kernel.require 'set'
require 'mycorp/missing'

module MyCorp
  VERSION = Version.current

  def self.boot
    Store.open
    Cli.run
  end
end
