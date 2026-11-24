# omnicheck (npm shim)

Executes the native `oc` binary through npm. Nothing is reimplemented in
JavaScript: the shim resolves the binary (OC_BIN env var, a binary
shipped at `bin/native/oc` from the release tarball, or the
`@omnicheck/oc-linux-*` platform package), then execs it with inherited
stdio and the child's exact exit code.

```sh
npm install -g ./npm                 # after scripts/package.sh put bin/native/oc in place
oc check examples --packs-dir packs  # identical bytes to the C build's output
```

Exit codes pass through unchanged: 0 clean, 1 findings >= threshold /
BREAKING / surviving mutants, 2 tool error or files that failed to parse
cleanly. The deterministic analysis contract (byte-identical reports,
named parse errors, no silent partial analysis) is a property of the
native binary; the shim only forwards it.
