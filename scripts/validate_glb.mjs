// Validates .glb files with the Khronos glTF Validator (npm package gltf-validator, pinned in
// tests/gltf-validator/package.json). Prints the validator's full report as JSON per file.
// Exit code 0: no errors and no warnings; 1: the validator reports an error or a warning;
// 2: usage or a file that cannot be read.
//
// Usage: node scripts/validate_glb.mjs file.glb [more.glb ...]
//   (run `npm ci` in tests/gltf-validator first, once)
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, resolve, basename } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(resolve(here, '..', 'tests', 'gltf-validator', 'package.json'));
const validator = require('gltf-validator');

const files = process.argv.slice(2);
if (files.length === 0) {
  console.error('usage: node scripts/validate_glb.mjs file.glb [more.glb ...]');
  process.exit(2);
}
let status = 0;
for (const file of files) {
  let bytes;
  try {
    bytes = new Uint8Array(readFileSync(file));
  } catch (e) {
    console.error(`cannot read ${file}: ${e.message}`);
    process.exit(2);
  }
  const report = await validator.validateBytes(bytes, { uri: basename(file), maxIssues: 0 });
  console.log(`== ${file} (validator ${validator.version()})`);
  console.log(JSON.stringify(report, null, 2));
  const issues = report.issues;
  if (issues.numErrors > 0 || issues.numWarnings > 0) {
    status = 1;
  }
}
process.exit(status);
