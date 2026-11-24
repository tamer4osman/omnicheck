# frozen_string_literal: true

# The require-shape guards: an interpolated payload and a multi-argument
# call record NOTHING (never guessed -- docs/LANGUAGES.md Ruby section 5).
def cli_dynamic(dir)
  require "#{dir}/engine"
end

def cli_multi
  require 'engine', 'render'
end

def cli_run
  puts 'cli'
end
