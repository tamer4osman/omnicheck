#!/usr/bin/env node
/*
 * OmniCheck npm shim (M5 GA packaging, plan P4).
 *
 * Resolution order, first hit wins:
 *   1. OC_BIN environment variable (pinned installs, CI templates);
 *   2. the platform binary shipped inside this package at
 *      bin/native/oc  (the `scripts/package.sh` layout, if installed
 *      from the release tarball via `npm install --prefix`);
 *   3. a sibling platform package `@omnicheck/oc-linux-x64` /
 *      `@omnicheck/oc-linux-arm64` (optionalDependencies pattern).
 *
 * The shim NEVER reimplements analysis: it execs the native binary with
 * the parent's stdio and exits with the child's exact exit code, so
 * `oc check` semantics (0 clean / 1 threshold / 2 parse or tool error)
 * pass through npm unchanged.
 */
'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

function resolveBinary() {
  if (process.env.OC_BIN && fs.existsSync(process.env.OC_BIN)) {
    return process.env.OC_BIN;
  }
  const local = path.join(__dirname, 'native', 'oc');
  if (fs.existsSync(local)) {
    return local;
  }
  const arch = process.arch === 'arm64' ? 'arm64' : 'x64';
  const candidates = [
    `@omnicheck/oc-linux-${arch}`,
    '@omnicheck/oc-linux-x64', // x64 hosts fall back explicitly
  ];
  for (const name of candidates) {
    try {
      const dir = path.join(
        path.dirname(require.resolve(`${name}/package.json`, { paths: [__dirname] })),
        'bin', 'oc');
      if (fs.existsSync(dir)) return dir;
    } catch (_) { /* package absent: try the next candidate */ }
  }
  process.stderr.write(
    'omnicheck: native binary not found.\n' +
    '  Looked at: OC_BIN, bin/native/oc, optional platform packages.\n' +
    '  Install the release tarball (scripts/package.sh) or set OC_BIN.\n');
  process.exit(2); // omnicheck tool-error exit code, honest passthrough
}

const bin = resolveBinary();
const r = spawnSync(bin, process.argv.slice(2), { stdio: 'inherit' });
if (r.error) {
  process.stderr.write(`omnicheck: failed to exec ${bin}: ${r.error.message}\n`);
  process.exit(2);
}
process.exit(r.status === null ? 2 : r.status);
