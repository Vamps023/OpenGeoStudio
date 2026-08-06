#!/usr/bin/env node
/**
 * Post-install: fetch the prebuilt better-sqlite3 binary matching the
 * Electron runtime's Node ABI, instead of the plain-Node ABI that npm
 * installs by default. better-sqlite3 is a native addon (not N-API), so
 * a binary built for Node's ABI cannot load inside Electron's process
 * and vice versa.
 *
 * This must run after every `npm install` because npm always installs
 * the Node-ABI build first.
 */
const { execFileSync } = require('child_process');
const path = require('path');

function getElectronVersion() {
  try {
    return require(path.join(process.cwd(), 'node_modules', 'electron', 'package.json')).version;
  } catch {
    return null;
  }
}

function hasBetterSqlite3() {
  try {
    require.resolve('better-sqlite3/package.json', { paths: [process.cwd()] });
    return true;
  } catch {
    return false;
  }
}

const electronVersion = getElectronVersion();
if (!electronVersion) {
  // No Electron installed (e.g. CI running only unit tests) — nothing to do.
  process.exit(0);
}
if (!hasBetterSqlite3()) {
  process.exit(0);
}

const cwd = path.join(process.cwd(), 'node_modules', 'better-sqlite3');
const prebuildInstallBin = require.resolve('prebuild-install/bin.js', { paths: [cwd] });

console.log(`[postinstall] Fetching better-sqlite3 prebuild for Electron ${electronVersion}...`);
try {
  execFileSync(
    process.execPath,
    [
      prebuildInstallBin,
      '--runtime=electron',
      `--target=${electronVersion}`,
      `--arch=${process.arch}`,
      `--platform=${process.platform}`,
    ],
    { cwd, stdio: 'inherit' }
  );
  console.log('[postinstall] better-sqlite3 rebuilt for Electron successfully.');
} catch (err) {
  console.warn('[postinstall] Could not fetch Electron prebuild for better-sqlite3:', err.message);
  console.warn('[postinstall] GeoPackage features may not work until this is resolved manually.');
}
