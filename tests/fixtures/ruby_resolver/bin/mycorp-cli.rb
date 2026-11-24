#!/usr/bin/env ruby
# frozen_string_literal: true

# The bundler-style executable: the shebang hint claims this file as an
# entry point.
require 'json'

puts JSON.generate(tool: 'mycorp-cli')
