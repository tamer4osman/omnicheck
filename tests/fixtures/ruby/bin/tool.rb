#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'

# The bin/ script: the pre-existing shebang hint claims this file as an
# entry point (#! is a comment to the Ruby parser, so a #! first line is
# ALWAYS a kernel shebang for Ruby -- docs/LANGUAGES.md Ruby section 6).
Tool = Struct.new(:name) do
  def run
    puts JSON.generate(name: name)
  end
end

Tool.new('tool').run if $PROGRAM_NAME == __FILE__
