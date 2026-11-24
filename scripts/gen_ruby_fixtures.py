#!/usr/bin/env python3
"""Generate the Ruby language fixtures (SPRINT-16, the gen_rust_fixtures
convention: the committed bytes pin the contract BEFORE/with the resolver,
and re-running this script must reproduce them byte-identically).

Corpora:
  tests/fixtures/ruby          -- structural, 7 .rb files: symbols (method/
                                  singleton_method/endless/bodyless/operator/
                                  setter), the comp-10 and comp-35 hand-counted
                                  decision inventories, the 69-line OC001
                                  method, one TODO (OC002), one duplicate group
                                  (OC008, 2 locations), one AKIA credential
                                  (OC011), a shebang bin/ script (entry), a
                                  runner-named test file; all requires name
                                  external gems (zero imports edges on this
                                  corpus -- criterion 4 of the lang gate).
  tests/fixtures/ruby_resolver -- resolver v1-ruby, 11 .rb files + the decoy
                                  Gemfile: require_relative (sibling, child,
                                  `..` traversal), bare require through the
                                  lib load-root convention, the a/b require
                                  cycle (OC003), external/interpolated/
                                  multi-arg/receiver-form decoys, an unreferenced
                                  file + an orphan (OC006), no licence file
                                  (OC010 on the Gemfile).

Usage: python3 scripts/gen_ruby_fixtures.py   (run from anywhere; idempotent)
"""
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

APP_RB = '''# frozen_string_literal: true

# The structural corpus "app" file (SPRINT-16): symbols, the comp-10
# hand-count, the OC001 long method and the OC002 marker.
#
# Every require on this corpus names an external gem: NOTHING here names a
# file inside the analyzed set, so `oc graph` must show ZERO imports edges
# -- an edge always means "the target is part of this analysis", never a
# guess (docs/LANGUAGES.md Ruby section 5). Positive resolution is pinned
# by tests/fixtures/ruby_resolver.
require 'json'
require 'set'

# App is export surface (a top-level class).
class App
  # comp-10: the hand-counted decision inventory -- one decision per
  # recorded kind (docs/LANGUAGES.md Ruby section 4). Decisions: if(1),
  # elsif(1), while(1), for(1), when(2 -- one per clause, the
  # comma-separated patterns are ONE clause), ternary(1), &&(1) = 7 ->
  # complexity 8? NO: the inventory below is 9 decisions -> comp 10.
  def flow(x)
    if x > 0
      sign = 1
    elsif x == 0
      sign = 0
    else
      sign = -1
    end
    while x > 100
      x -= 100
    end
    for i in 0..2
      sign += i
    end
    case x
    when 0, 1 then tag = :low
    when Integer then tag = :num
    else tag = :other
    end
    z = sign.positive? ? 1 : 0
    z += 1 if x.positive? && sign.positive?
    z
  end

  # A singleton method is a symbol with the same span contract.
  def self.build(name)
    new(name)
  end

  # An endless definition is the same method node (Ruby 3.0+).
  def short_name = :endless

  # An empty method is a symbol with complexity exactly 1.
  def empty_hook; end

  # A setter keeps the name as written.
  def name=(v)
    @name = v
  end

  # An operator method keeps the name as written.
  def [](idx)
    idx
  end
end

# TODO: split the long method below once the report shape is settled.
def long_runner(seed)
  acc = seed
  acc += 1
  acc += 2
  acc += 3
  acc += 4
  acc += 5
  acc += 6
  acc += 7
  acc += 8
  acc += 9
  acc += 10
  acc += 11
  acc += 12
  acc += 13
  acc += 14
  acc += 15
  acc += 16
  acc += 17
  acc += 18
  acc += 19
  acc += 20
  acc += 21
  acc += 22
  acc += 23
  acc += 24
  acc += 25
  acc += 26
  acc += 27
  acc += 28
  acc += 29
  acc += 30
  acc += 31
  acc += 32
  acc += 33
  acc += 34
  acc += 35
  acc += 36
  acc += 37
  acc += 38
  acc += 39
  acc += 40
  acc += 41
  acc += 42
  acc += 43
  acc += 44
  acc += 45
  acc += 46
  acc += 47
  acc += 48
  acc += 49
  acc += 50
  acc += 51
  acc += 52
  acc += 53
  acc += 54
  acc += 55
  acc += 56
  acc += 57
  acc += 58
  acc += 59
  acc += 60
  acc += 61
  acc += 62
  acc
end
'''

MATHX_RB = '''# frozen_string_literal: true

require 'json'

# MathX is export surface (a top-level module). The comp-35 method is the
# hand-counted decision inventory of docs/LANGUAGES.md Ruby section 4 --
# 34 decisions, one per recorded kind (the Ruby analogue of the Rust
# comp-35: ONE decision per when clause, the comma patterns are ONE
# clause; an if_guard inside an in_clause does NOT count separately).
module MathX
  INVENTORY = [1, 2, 3].freeze

  # Decisions (34): if(1) + elsif(2) + unless(1) + while(1) + until(1)
  # + for(1) + when(4) + in_clause(3) + rescue(3) + rescue_modifier(1)
  # + if_modifier(2) + unless_modifier(1) + while_modifier(1)
  # + until_modifier(1) + conditional(1) = 24 kind decisions, plus
  # &&(6) + ||(2) + and(1) + or(1) = 10 operator decisions -> 34 total,
  # complexity 35.
  def inventory(x, ys)
    total = 0
    if x.positive? && ys.size > 0
      base = 1
    elsif x.zero? && ys.empty? || ys.nil?
      base = 2
    elsif x.negative?
      base = 3
    else
      base = 4
    end
    unless base == 4
      total += base
    end
    while x > 10
      x -= 10
    end
    until x <= 0 && total > 0
      x -= 1
    end
    for y in ys
      total += y
    end
    case x
    when 0 then total += 1
    when 1 then total += 2
    when 2, 3 then total += 3
    when Integer then total += 4
    else total += 5
    end
    case x
    in Integer => n then total += n
    in [a, b] if a then total += b
    in String then total += 1
    else total += 0
    end
    begin
      total /= x
    rescue ZeroDivisionError
      total = 0
    rescue TypeError, RangeError => e
      total = -1
    rescue StandardError
      total = -2
    ensure
      total += 0
    end
    fallback = total rescue 0
    fallback += 1 unless x.nil?
    fallback += 1 while x > 99
    fallback += 1 until x > 999
    fallback = x.positive? ? fallback : 0
    fallback += 1 if x.positive? && total.positive? && base.positive? && fallback >= 0
    fallback += 1 if ys.empty? || x.zero?
    fallback = 1 and fallback = 2
    fallback = 3 or fallback = 4
    fallback
  end

  # A bodyless (empty) method: a symbol with complexity exactly 1.
  def hook; end
end
'''

DUP_BODY = '''    total = 0
    rows.each do |r|
      next unless r.valid?
      total += r.value
      audit << r.tag
      seen[r.key] = true
    end
    total
  end

  def second_page(rows, audit, seen)
'''

DUP_A_RB = '''# frozen_string_literal: true

require 'json'

# The OC008 duplicate-code pair: the same 10-line normalized block in two
# files (one group, two locations -- the rust corpus convention).
class Ledger
  def first_page(rows, audit, seen)
''' + DUP_BODY + '''    total
  end
end
'''

DUP_B_RB = '''# frozen_string_literal: true

require 'json'

# The OC008 duplicate-code pair: the same 10-line normalized block in two
# files (one group, two locations -- the rust corpus convention).
class LedgerB
  def first_page(rows, audit, seen)
''' + DUP_BODY + '''    total
  end
end
'''

SECRET_RB = '''# frozen_string_literal: true

require 'json'

# The OC011 corpus file: exactly one credential-shaped literal (an AWS
# access key id); OC006 also fires here (the file is never required).
CONFIG = {
  aws_access_key_id: 'AKIAIOSFODNN7EXAMPLE',
  region: 'us-east-1'
}.freeze
'''

TOOL_RB = '''#!/usr/bin/env ruby
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
'''

MATHX_TEST_RB = '''# frozen_string_literal: true

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
'''

RESOLVER_FILES = {
    'lib/mycorp.rb': '''# frozen_string_literal: true

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
''',
    'lib/mycorp/version.rb': '''# frozen_string_literal: true

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
''',
    'lib/mycorp/cli.rb': '''# frozen_string_literal: true

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
''',
    'lib/mycorp/store.rb': '''# frozen_string_literal: true

# Required by lib/mycorp.rb through the bare require (the <root>/lib
# load-root convention). Its require_relative child resolves to
# lib/mycorp/store/types.rb.
require_relative 'store/types'

class Store
  def self.open
    Types.load
  end
end
''',
    'lib/mycorp/store/types.rb': '''# frozen_string_literal: true

# Required by lib/mycorp/store.rb through require_relative.
class Types
  def self.load
    []
  end
end
''',
    'lib/mycorp/store/orphan.rb': '''# frozen_string_literal: true

# Never required by anyone: OC006 fires here (the honest over-report
# direction -- a verdict a triage can clear).
class Orphan
  def stand_alone
    1
  end
end
''',
    'lib/mycorp/lonely.rb': '''# frozen_string_literal: true

# Never required by anyone: OC006 fires here. The Gemfile DEPENDENCIES
# decoy naming the gem is never read BY THE RESOLVER (the manifest is
# supply-tier data, not load-path data).
class Lonely
  def wander
    2
  end
end
''',
    'lib/mycorp/a.rb': '''# frozen_string_literal: true

# The require cycle, leg one: the bare require hits the lib load root
# (lib/mycorp/b.rb).
require 'mycorp/b'

class CycleA
  def ping
    CycleB.new.pong
  end
end
''',
    'lib/mycorp/b.rb': '''# frozen_string_literal: true

# The require cycle, leg two: require_relative (the sibling channel).
# require cycles are LEGAL Ruby at runtime ($LOADED_FEATURES dedupes the
# second load); statically the cycle is a real import shape and OC003 is
# the honest finding (docs/LANGUAGES.md Ruby section 5).
require_relative 'a'

class CycleB
  def pong
    CycleA.new.ping
  end
end
''',
    'bin/mycorp-cli.rb': '''#!/usr/bin/env ruby
# frozen_string_literal: true

# The bundler-style executable: the shebang hint claims this file as an
# entry point.
require 'json'

puts JSON.generate(tool: 'mycorp-cli')
''',
    'test/mycorp/store_test.rb': '''# frozen_string_literal: true

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
''',
}

GEMFILE = '''# frozen_string_literal: true

source 'https://rubygems.org'

# The manifest tier fixture (the resolver corpus decoy + supply pin):
#   mycorp  '1.2.3'  -- the bare version form IS a pin (bundler's own
#                       exact-requirement semantics)
#   rake    '~> 13.0' -- a comparator range is NOT a pin (unpinned warn)
#   inrepo  path:     -- an in-repo link, not OC009 material (the Cargo
#                        path-dep precedent)
# The commented gem below is a comment, never a dependency:
#   gem 'ghost', '9.9.9'
gem 'mycorp', '1.2.3'
gem 'rake', '~> 13.0'
gem 'inrepo', path: '../inrepo'
gem 'nopin'
'''

GEMFILE_LOCK = '''GEM
  remote: https://rubygems.org/
  specs:
    mycorp (1.2.3)
    rake (13.0.6)
      racc (~> 1.4)
    json (2.7.1)
    bigdecimal (3.1.6)

GIT
  remote: https://example.com/edge/edgegem.git
  revision: deadbeefdeadbeefdeadbeefdeadbeefdeadbeef
  specs:
    edgegem (0.1.0)

PATH
  remote: ../inrepo
  specs:
    inrepo (0.2.0)

PLATFORMS
  ruby
  x86_64-linux

DEPENDENCIES
  mycorp (~> 1.0)
  rake
  inrepo!

BUNDLED WITH
   2.5.6
'''


def write(rel, content):
    path = os.path.join(REPO, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='\n') as fh:
        fh.write(content)
    print("wrote", rel)


def main():
    write('tests/fixtures/ruby/app.rb', APP_RB)
    write('tests/fixtures/ruby/mathx.rb', MATHX_RB)
    write('tests/fixtures/ruby/util/dup_a.rb', DUP_A_RB)
    write('tests/fixtures/ruby/util/dup_b.rb', DUP_B_RB)
    write('tests/fixtures/ruby/cfg/secret.rb', SECRET_RB)
    write('tests/fixtures/ruby/bin/tool.rb', TOOL_RB)
    write('tests/fixtures/ruby/mathx_test.rb', MATHX_TEST_RB)
    for rel, content in RESOLVER_FILES.items():
        write('tests/fixtures/ruby_resolver/' + rel, content)
    write('tests/fixtures/ruby_resolver/Gemfile', GEMFILE)
    write('tests/fixtures/ruby_resolver/Gemfile.lock', GEMFILE_LOCK)


if __name__ == '__main__':
    main()
