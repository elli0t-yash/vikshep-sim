#!/usr/bin/env python3
"""geometry.glb of both examples passes the Khronos glTF Validator with no error and no warning.

Usage: check_glb_validator.py <vikshep-sim> <examples dir> <repository root>
Needs node and the pinned validator (`npm ci` in tests/gltf-validator). Exit code 77 (ctest:
skipped) when the Geant4 data sets, node or the validator are missing; with VIKSHEP_REQUIRE_DATA
set (CI) a missing piece is a failure instead.
A damaged file must be reported too, so that a validator that checks nothing cannot pass.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vikshep_testlib  # noqa: E402

exe, examples, root = sys.argv[1:4]
vikshep_testlib.require_data_sets()

must = os.environ.get("VIKSHEP_REQUIRE_DATA", "") not in ("", "0")
node = shutil.which("node")
validator_dir = os.path.join(root, "tests", "gltf-validator", "node_modules", "gltf-validator")
if node is None or not os.path.isdir(validator_dir):
    sys.stderr.write("%s: node or tests/gltf-validator/node_modules is missing (run `npm ci` there)\n" %
                     ("FAILED (VIKSHEP_REQUIRE_DATA is set)" if must else "SKIPPED, not passed"))
    sys.exit(1 if must else 77)

script = os.path.join(root, "scripts", "validate_glb.mjs")
work = tempfile.mkdtemp(prefix="vikshep-glb-")
problems = []


def validate(path):
    proc = subprocess.run([node, script, path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    body = proc.stdout[proc.stdout.index("{"):] if "{" in proc.stdout else "{}"
    return proc.returncode, json.loads(body), proc.stderr


try:
    for name in ("tracker_muon", "calorimeter"):
        out = os.path.join(work, name)
        run = subprocess.run([exe, "run", "--spec", os.path.join(examples, name + ".json"), "--out", out,
                              "--progress", "none", "--export-3d"], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
        if run.returncode != 0:
            problems.append("%s: run failed with exit code %d" % (name, run.returncode))
            continue
        glb = os.path.join(out, "geometry.glb")
        code, report, err = validate(glb)
        issues = report.get("issues", {})
        counts = {k: issues.get(k) for k in ("numErrors", "numWarnings", "numInfos", "numHints")}
        ok = code == 0 and all(v == 0 for v in counts.values())
        print("%s %s: validator %s, %s" % ("PASS " if ok else "FAIL ", name, report.get("validatorVersion"), counts))
        if not ok:
            problems.append("%s: %s %s" % (name, counts, issues.get("messages")))
        # a damaged copy (last byte of the file removed, so the container length is wrong) is reported
        data = open(glb, "rb").read()
        bad = os.path.join(work, name + "-damaged.glb")
        open(bad, "wb").write(data[:-1])
        code, report, err = validate(bad)
        flagged = code == 1 and report.get("issues", {}).get("numErrors", 0) > 0
        print("%s %s: a damaged copy is reported as invalid" % ("PASS " if flagged else "FAIL ", name))
        if not flagged:
            problems.append("%s: the validator did not flag a truncated file" % name)
finally:
    shutil.rmtree(work, ignore_errors=True)

if problems:
    print("\n%d check(s) failed:\n  %s" % (len(problems), "\n  ".join(problems)))
    sys.exit(1)
print("\nall glTF validator checks passed")
