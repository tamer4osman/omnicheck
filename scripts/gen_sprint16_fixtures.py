#!/usr/bin/env python3
"""Generate the SPRINT-16 unit fixtures (tests/fixtures_sprint16/, the
gen_sprint15_fixtures convention: re-running must reproduce the bytes).

  rubycov/good.resultset.json   -- valid SimpleCov: two suites (union), null
                                   not-relevant lines, a branches map, hit
                                   counts > 0.
  rubycov/bad_elem.resultset.json  -- a lines element that is neither a hit
                                      count nor null (loud USAGE).
  rubycov/no_lines.resultset.json  -- a file entry without a lines array
                                      (loud USAGE).
  rubycov/mixed_suite.resultset.json -- a suite that is not an object next
                                      to a dispatching suite (loud USAGE).
  gemfile/Gemfile                  -- pins (bare version + "=" form), the
                                      comparator ranges (NOT pins), a path:
                                      in-repo link, a commented-out gem, a
                                      no-spec gem.
  gemfile_lock/Gemfile.lock        -- GEM>specs resolved pins (4-space), the
                                      6-space requested sub-entries, a GIT
                                      section and DEPENDENCIES (record
                                      nothing).
  gemfile_lic/{Gemfile,LICENSE}    -- the licence probe positive (the root
                                      licence file makes OC010 silent).
  osv_snapshot_ruby.json           -- rake in 0..<14.0.0 (the lockfile's
                                      13.0.6 pin matches, ERROR), json
                                      introduced 99.0 (2.7.1 does not).

Usage: python3 scripts/gen_sprint16_fixtures.py
"""
import json
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

GOOD_RESULTSET = {
    "RSpec": {
        "coverage": {
            "app/models/user.rb": {
                "lines": [1, 1, 0, None, None, 2, 1, 0],
                "branches": {"[:if, 0, 3, 4, 3, 6]": {"[:then, 1]": 0, "[:else, 2]": 1}},
            },
            "app/services/billing.rb": {
                "lines": [None, 3, 3, 0],
            },
        },
        "timestamp": "2026-11-22T10:00:00Z",
    },
    "Minitest": {
        "coverage": {
            "app/models/user.rb": {
                "lines": [1, None, 1, None, None, 0, 1, None],
            },
        },
        "timestamp": "2026-11-22T10:05:00Z",
    },
}

BAD_ELEM_RESULTSET = {
    "RSpec": {
        "coverage": {
            "app/models/user.rb": {"lines": [1, "oops", 0]},
        }
    }
}

NO_LINES_RESULTSET = {
    "RSpec": {
        "coverage": {
            "app/models/user.rb": {"branches": {}},
        }
    }
}

MIXED_SUITE_RESULTSET = {
    "RSpec": "this suite is not an object",
    "Minitest": {
        "coverage": {
            "app/models/user.rb": {"lines": [1, 0]},
        }
    },
}

GEMFILE = """# frozen_string_literal: true

source 'https://rubygems.org'

# The supply-tier unit fixture (docs/LANGUAGES.md Ruby section 6):
#   pinned  '1.2.3'    -- the bare version form IS a pin
#   exact   '= 2.0.1'  -- the explicit "= x.y.z" form is a pin
#   tilde   '~> 3.1'   -- a comparator range is NOT a pin
#   ranged  '>= 4.0'   -- NOT a pin
#   excluded path:     -- an in-repo link, not OC009 material
#   nospec             -- no spec at all: unpinned
# The commented gem below is a comment, never a dependency:
#   gem 'ghost', '9.9.9'
gem 'pinned', '1.2.3'
gem 'exact', '= 2.0.1'
gem 'tilde', '~> 3.1'
gem 'ranged', '>= 4.0'
gem 'excluded', path: '../excluded'
gem 'nospec'
"""

GEMFILE_LOCK = """GEM
  remote: https://rubygems.org/
  specs:
    exact (2.0.1)
    pinned (1.2.3)
    rake (13.0.6)
      racc (~> 1.4)
    tilde (3.1.4)
    json (2.7.1)

GIT
  remote: https://example.com/edge/edgegem.git
  revision: deadbeefdeadbeefdeadbeefdeadbeefdeadbeef
  specs:
    edgegem (0.1.0)

PATH
  remote: ../excluded
  specs:
    excluded (0.2.0)

PLATFORMS
  ruby
  x86_64-linux

DEPENDENCIES
  pinned (~> 1.0)
  exact
  excluded!

BUNDLED WITH
   2.5.6
"""

GEMFILE_LIC = """# frozen_string_literal: true

source 'https://rubygems.org'

gem 'licensed', '1.0.0'
"""

LICENSE_TXT = """MIT License

Copyright (c) 2026 the fixture authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction.
"""

OSV_SNAPSHOT = {
    "schema": "omnicheck.osv_snapshot/v0",
    "snapshot_date": "2026-11-22",
    "records": [
        {
            "id": "GHSA-ruby-rake-0001",
            "affected": [
                {
                    "package": {"name": "rake", "ecosystem": "rubygems"},
                    "ranges": [
                        {"type": "ECOSYSTEM",
                         "events": [{"introduced": "0"}, {"fixed": "14.0.0"}]}
                    ],
                }
            ],
        },
        {
            "id": "GHSA-ruby-json-0002",
            "affected": [
                {
                    "package": {"name": "json", "ecosystem": "rubygems"},
                    "ranges": [
                        {"type": "ECOSYSTEM",
                         "events": [{"introduced": "99.0"}]}
                    ],
                }
            ],
        },
    ],
}


def write_text(rel, content):
    path = os.path.join(REPO, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='\n') as fh:
        fh.write(content)
    print("wrote", rel)


def write_json(rel, obj):
    path = os.path.join(REPO, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', newline='\n') as fh:
        json.dump(obj, fh, indent=1)
        fh.write("\n")
    print("wrote", rel)


def main():
    write_json('tests/fixtures_sprint16/rubycov/good.resultset.json', GOOD_RESULTSET)
    write_json('tests/fixtures_sprint16/rubycov/bad_elem.resultset.json', BAD_ELEM_RESULTSET)
    write_json('tests/fixtures_sprint16/rubycov/no_lines.resultset.json', NO_LINES_RESULTSET)
    write_json('tests/fixtures_sprint16/rubycov/mixed_suite.resultset.json', MIXED_SUITE_RESULTSET)
    write_text('tests/fixtures_sprint16/gemfile/Gemfile', GEMFILE)
    write_text('tests/fixtures_sprint16/gemfile_lock/Gemfile.lock', GEMFILE_LOCK)
    write_text('tests/fixtures_sprint16/gemfile_lic/Gemfile', GEMFILE_LIC)
    write_text('tests/fixtures_sprint16/gemfile_lic/LICENSE', LICENSE_TXT)
    write_json('tests/fixtures_sprint16/osv_snapshot_ruby.json', OSV_SNAPSHOT)


if __name__ == '__main__':
    main()
