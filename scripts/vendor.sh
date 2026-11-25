#!/bin/sh
# Fetch vendored third-party sources at pinned refs.
# Contributors cloning the git repo MUST run this once before building.
# Release tarballs ship third_party/ already vendored and skip this step.
set -eu
cd "$(dirname "$0")/.."

# Portable sha256: GNU coreutils' sha256sum is not present on macOS, which
# ships shasum instead (openssl is the last-resort fallback).
sha256_of() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum > /dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v openssl > /dev/null 2>&1; then
        openssl dgst -sha256 "$1" | awk '{print $NF}'
    else
        return 1
    fi
}

# Verify a sha256sum-style manifest ("<hash>  <path>" per line); portable
# replacement for "sha256sum -c" so the check runs on macOS too.
verify_manifest() {
    manifest="$1"
    failed=0
    while read -r want path _; do
        [ -n "$want" ] || continue
        case "$want" in \#*) continue ;; esac
        got=$(sha256_of "$path") || return 1
        if [ "$got" != "$want" ]; then
            echo "vendor: sha256 mismatch: $path"
            failed=1
        fi
    done < "$manifest"
    return $failed
}

TS_REF="${TS_REF:-v0.27.2}"
JS_REF="${JS_REF:-v0.25.0}"
PY_REF="${PY_REF:-v0.25.0}"
TS_TS_REF="${TS_TS_REF:-v0.23.2}"
GO_REF="${GO_REF:-v0.25.0}"
JAVA_REF="${JAVA_REF:-v0.23.5}"
CSHARP_REF="${CSHARP_REF:-v0.24.0}"
RUST_REF="${RUST_REF:-v0.24.0}"
RUBY_REF="${RUBY_REF:-v0.23.1}"
PHP_REF="${PHP_REF:-v0.24.2}"
C_REF="${C_REF:-v0.23.4}"
CPP_REF="${CPP_REF:-v0.23.4}"
KOTLIN_REF="${KOTLIN_REF:-0.3.8}"
SWIFT_REF="${SWIFT_REF:-0.7.1-with-generated-files}"
SCALA_REF="${SCALA_REF:-v0.23.4}"

# Alternate provenance: the npm binding package tarballs of the pinned
# versions (registry.npmjs.org), the same sources the git tags package --
# each npm parser.c byte-verified identical to the vendored tree at
# adoption (SPRINT-13 m1-replay drift leg: the environment's github
# codeload endpoint throttled for the grammars-org repos, so every
# grammar carries a FINAL sha256-verified fallback that reproduces the
# vendored tree byte for byte; the npm package mirrors the repo src/
# layout). The tree-sitter RUNTIME keeps codeload as its only fallback
# (no alternate pinned provenance exists at v0.27.2).
JS_NPM_PKG="tree-sitter-javascript"
JS_NPM_SHA256="6214ce23d03aba525ba8edd6ca7caf365c969f2c2dbfc353e376ca54507ffb92"
PY_NPM_PKG="tree-sitter-python"
PY_NPM_SHA256="63b76b3fa8181fd79eaad4abcdb21e2babcb504dbfc7710a89934fa456d26096"
TS_TS_NPM_PKG="tree-sitter-typescript"
TS_TS_NPM_SHA256="0fdf63c35930a75885145d75ee63cb879da2d49db9bfe454bd8e60b08ba778a1"
GO_NPM_PKG="tree-sitter-go"
GO_NPM_SHA256="f7265544d6b297797dc3675c02b76fb182c177ce29fda10ac1f36e9ab33fd90e"
JAVA_NPM_PKG="tree-sitter-java"
JAVA_NPM_SHA256="6b6c1b62605f9d8bb80efd2ebe5951a87b182faafa3eeb67c460a667818551b6"
CSHARP_NPM_PKG="tree-sitter-c-sharp"
CSHARP_NPM_SHA256="8077a5ccc21f071ee92444dce678bd9a1f2908e5d4c4bd3d63f395c5316f81fe"
RUST_NPM_PKG="tree-sitter-rust"
RUST_NPM_SHA256="4248da0b9ea40fec13ef010822e7b92a9d1ebdf74f6f2733539b1dbbb086c5bb"
RUBY_NPM_PKG="tree-sitter-ruby"
RUBY_NPM_SHA256="1f0f0c100e31ddf650962ebb1f1fb8420bdbabccd49582f6e731ea7478db639d"
PHP_NPM_PKG="tree-sitter-php"
PHP_NPM_SHA256="fdd22530d3845540ba74a1199d3f67204a608c99994bb423645d3f0cfdaead94"
C_NPM_PKG="tree-sitter-c"
C_NPM_SHA256="31f07c7c29748a2cf93ac1e0e1363324689f5ebf39c5b012081cb677a1392f5a"
CPP_NPM_PKG="tree-sitter-cpp"
CPP_NPM_SHA256="615d152f70e7a6bed8f15a93c3c0c264c40dc71c1b6a1445646fff9439e776bc"
KOTLIN_NPM_PKG="tree-sitter-kotlin"
KOTLIN_NPM_SHA256="e2da896c8316af1d491afc69b49173da97373bb757d07f11c455a0963c62e6b0"
SWIFT_NPM_PKG="tree-sitter-swift"
SWIFT_NPM_SHA256="abd89284f1b0f1375ba5efaa9ddf99ad4b16ac91fc7751ef10c5f013cca06f8c"
SWIFT_NPM_VER="0.7.1"
SCALA_NPM_PKG="tree-sitter-scala"
SCALA_NPM_SHA256="c4c92b40d00b7d3a2ca1412ebadefae52963dd000f0e2d683afa572eff1bb171"
SCALA_NPM_VER="0.23.4"

fetch() {
    repo="$1"; ref="$2"; dest="$3"; npm_pkg="${4:-}"; npm_sha="${5:-}"; npm_ver="${6:-}"
    if [ -d "$dest" ]; then
        echo "vendor: $dest already present, skipping (delete it to re-vendor)"
        return 0
    fi
    echo "vendor: $repo @ $ref -> $dest"
    if git clone --depth 1 --branch "$ref" "https://github.com/$repo.git" "$dest" 2>/dev/null; then
        return 0
    fi
    # Fallback for environments where the github git protocol is
    # unavailable: the codeload tarball of the SAME pinned tag is
    # content-identical to the tag checkout. Applied first to
    # tree-sitter-go (SPRINT-9, build env had git-over-https blocked
    # for new repos); kept generic for every grammar.
    echo "vendor: git clone unavailable, trying codeload tarball for $repo @ $ref"
    tgz="$dest.tar.gz"
    if curl -sSL -o "$tgz" "https://codeload.github.com/$repo/tar.gz/refs/tags/$ref" \
        && tar tzf "$tgz" > /dev/null 2>&1; then
        mkdir -p "$dest"
        tar xzf "$tgz" -C "$dest" --strip-components 1
        rm -f "$tgz"
        return 0
    fi
    rm -f "$tgz"
    # Final fallback (grammar-specific): the npm binding package tarball
    # of the SAME pinned version, sha256-verified before extraction --
    # so the fallback reproduces the vendored tree byte for byte.
    if [ -n "$npm_pkg" ]; then
        echo "vendor: codeload unavailable, trying npm package tarball for $npm_pkg @ ${npm_ver:-$ref}"
        ntar="$dest.npm.tgz"
        curl -sSL -o "$ntar" "https://registry.npmjs.org/$npm_pkg/-/$npm_pkg-${npm_ver:-${ref#v}}.tgz" \
            || { rm -f "$ntar"; echo "vendor: FAILED -- cannot fetch $repo @ $ref (git clone, codeload and npm all unavailable)"; exit 1; }
        got=$(sha256_of "$ntar") \
            || { rm -f "$ntar"; echo "vendor: FAILED -- no sha256 tool available"; exit 1; }
        [ "$got" = "$npm_sha" ] \
            || { rm -f "$ntar"; echo "vendor: FAILED -- npm tarball sha256 mismatch for $npm_pkg"; exit 1; }
        mkdir -p "$dest"
        tar xzf "$ntar" -C "$dest" --strip-components 1
        rm -f "$ntar"
        return 0
    fi
    echo "vendor: FAILED -- cannot fetch $repo @ $ref (git clone and codeload both unavailable)"; exit 1
}

mkdir -p third_party
cd third_party
fetch tree-sitter/tree-sitter          "$TS_REF" tree-sitter
fetch tree-sitter/tree-sitter-javascript "$JS_REF" tree-sitter-javascript "$JS_NPM_PKG" "$JS_NPM_SHA256"
fetch tree-sitter/tree-sitter-python   "$PY_REF" tree-sitter-python "$PY_NPM_PKG" "$PY_NPM_SHA256"
fetch tree-sitter/tree-sitter-typescript "$TS_TS_REF" tree-sitter-typescript "$TS_TS_NPM_PKG" "$TS_TS_NPM_SHA256"
fetch tree-sitter-grammars/tree-sitter-go "$GO_REF" tree-sitter-go "$GO_NPM_PKG" "$GO_NPM_SHA256"
fetch tree-sitter/tree-sitter-java        "$JAVA_REF" tree-sitter-java "$JAVA_NPM_PKG" "$JAVA_NPM_SHA256"
fetch tree-sitter-grammars/tree-sitter-c-sharp "$CSHARP_REF" tree-sitter-c-sharp "$CSHARP_NPM_PKG" "$CSHARP_NPM_SHA256"
fetch tree-sitter/tree-sitter-rust           "$RUST_REF" tree-sitter-rust "$RUST_NPM_PKG" "$RUST_NPM_SHA256"
fetch tree-sitter/tree-sitter-ruby           "$RUBY_REF" tree-sitter-ruby "$RUBY_NPM_PKG" "$RUBY_NPM_SHA256"
# tree-sitter-php keeps the v0.22+ subdirectory layout at the repo root
# (php/ php_only/ common/) in BOTH provenances -- the npm package and the
# codeload tag are byte-identical there (SPRINT-19: parser.c, scanner.c,
# node-types.json cross-checked). OmniCheck compiles ONLY php/src/parser.c
# + php/src/scanner.c (the mixed php+html grammar); php_only stays
# uncompiled vendored bytes.
fetch tree-sitter/tree-sitter-php            "$PHP_REF" tree-sitter-php "$PHP_NPM_PKG" "$PHP_NPM_SHA256"
# tree-sitter-c has NO external scanner (pure parser.c + node-types.json,
# SPRINT-20 probe: src/ holds parser.c grammar.json node-types.json
# tree_sitter/ only); the npm package parser.c is byte-identical to the
# git tag (verified at adoption).
fetch tree-sitter/tree-sitter-c              "$C_REF" tree-sitter-c "$C_NPM_PKG" "$C_NPM_SHA256"
# tree-sitter-cpp (SPRINT-21, wave-2 close-out): generated parser.c (the
# FULL C grammar is inlined at generation time -- grammar.js requires
# tree-sitter-c/grammar -- so the parser is self-contained) PLUS an
# external scanner (src/scanner.c -- raw strings, angle-bracket
# disambiguation), which oc_add_grammar picks up automatically; vendored
# from the pinned v0.23.4 tag of the MAIN tree-sitter org (the same
# version number as the C pin). The npm package parser.c/scanner.c/
# node-types.json/grammar.js/LICENSE were verified byte-identical to the
# git tag at adoption. LANGUAGE_VERSION 14, accepted by the vendored
# runtime v0.27.2.
fetch tree-sitter/tree-sitter-cpp            "$CPP_REF" tree-sitter-cpp "$CPP_NPM_PKG" "$CPP_NPM_SHA256"
# tree-sitter-kotlin (SPRINT-22, wave-3 opens): generated parser.c
# (LANGUAGE_VERSION 14) PLUS an external scanner (src/scanner.c --
# automatic semicolons, string interpolation, nested multiline comments,
# safe navigation), which oc_add_grammar picks up automatically; src/ is
# self-contained. Vendored from the pinned 0.3.8 tag (the tag carries NO
# v prefix) of fwcd/tree-sitter-kotlin -- the canonical community
# grammar; the MAIN tree-sitter org has no Kotlin grammar. The npm
# binding package of the SAME version (fwcd's own tree-sitter-kotlin)
# had its parser.c/scanner.c/node-types.json/grammar.json/LICENSE
# verified byte-identical to the git tag at adoption.
fetch fwcd/tree-sitter-kotlin                "$KOTLIN_REF" tree-sitter-kotlin "$KOTLIN_NPM_PKG" "$KOTLIN_NPM_SHA256"
# tree-sitter-swift (SPRINT-23, wave-3 continues): the tag is
# 0.7.1-with-generated-files (the generated parser files ship on the
# dedicated tag) while the npm version is the plain 0.7.1 -- the npm
# package publishes FROM the -with-generated-files tag, so the two
# provenances are byte-identical for parser.c/scanner.c/node-types.json/
# grammar.json/LICENSE (verified at adoption). The explicit 6th arg
# carries the npm version.
fetch alex-pinkus/tree-sitter-swift          "$SWIFT_REF" tree-sitter-swift "$SWIFT_NPM_PKG" "$SWIFT_NPM_SHA256" "$SWIFT_NPM_VER"
# tree-sitter-scala (SPRINT-24, wave-3 continues): the MAIN tree-sitter
# org's grammar, tag v0.23.4. The 0.24.0 npm twin was regenerated from a
# different commit than the v0.24.0 git tag (20285 vs 19568 states), so
# the pin stays at v0.23.4 where the npm 0.23.4 package is byte-identical
# to the tag for parser.c/scanner.c/node-types.json/grammar.json/LICENSE
# (verified at adoption; scanner audited clean -- ts_calloc(1,...), no
# bit shifts, bounded serialize/deserialize).
fetch tree-sitter/tree-sitter-scala          "$SCALA_REF" tree-sitter-scala "$SCALA_NPM_PKG" "$SCALA_NPM_SHA256" "$SCALA_NPM_VER"

# OmniDecide-C is COMMITTED in-repo (vendor decision L8, see
# third_party/omnidecide/PROVENANCE.md) -- there is nothing to fetch, but
# the working tree is verified against the committed sha256 manifest so
# accidental edits to the vendored foreign sources are caught here.
if [ -f omnidecide/VENDOR.sha256 ] && [ -f omnidecide/PROVENANCE.md ]; then
    (cd omnidecide && verify_manifest VENDOR.sha256) \
        && echo "vendor: omnidecide integrity OK ($(wc -l < omnidecide/VENDOR.sha256) files)" \
        || { echo "vendor: FAILED -- third_party/omnidecide does not match VENDOR.sha256"; \
             echo "vendor: restore the committed tree (git checkout -- third_party/omnidecide) or re-vendor per PROVENANCE.md"; \
             exit 1; }
fi
echo "vendor: done"
