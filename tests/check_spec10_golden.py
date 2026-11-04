#!/usr/bin/env python3
"""Spec 1.0 files keep their meaning in a build that also accepts spec 1.1 (docs/SPEC.md, 2.7).

Usage: check_spec10_golden.py <vikshep-sim> <examples dir> <golden dir>
       check_spec10_golden.py <vikshep-sim> <examples dir> <golden dir> --record

Two kinds of check, on every spec 1.0 example (calorimeter, electron_shower, pion_shower):

  1. SPEC10_OIDS.txt: the spec_oid (OID rule applied to the canonical form), computed here with
     hashlib from the output of `validate --canonical`, equals the value recorded when the examples
     were written, before spec 1.1 existed. This does not depend on the platform.
  2. RUN_OIDS.txt: the OID of hits.arrow of the example run equals the value recorded for this
     platform and build (the key is the platform, compiler, Geant4 build and build type in
     provenance.json). Simulation output is promised only for the same build, platform and seed,
     so a build with no recorded value is reported and not compared: this check says nothing then,
     and the output says so. --record prints the lines for the current build.

Exit code 77 (ctest: skipped) when the Geant4 data sets are missing.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

exe, examples, golden = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
record = "--record" in sys.argv
NAMES = ["calorimeter", "electron_shower", "pion_shower"]

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vikshep_testlib  # noqa: E402

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


def oid(data):
    return hashlib.sha3_256(data).hexdigest()[:28]


def canonical_oid(name):
    out = subprocess.run([exe, "validate", "--spec", str(examples / (name + ".json")), "--canonical"],
                         stdout=subprocess.PIPE, check=True).stdout
    assert out.endswith(b"\n")
    return oid(out[:-1])  # the newline is not part of the hashed bytes


def read_table(path):
    rows = {}
    for line in path.read_text().splitlines():
        if line.strip():
            fields = line.split("\t") if "\t" in line else line.split()
            rows[tuple(fields[:-1])] = fields[-1]
    return rows


def platform_key(provenance):
    p = provenance["platform"]
    return "%s %s %s | geant4 %s %s | %s" % (p["os"], p["arch"], p["compiler"], provenance["geant4"]["version"],
                                              provenance["geant4_conda"]["build"], provenance["build_type"])


if record:
    vikshep_testlib.require_data_sets()
    work = tempfile.mkdtemp(prefix="vikshep-golden-")
    try:
        for name in NAMES:
            print("SPEC10\t%s\t%s" % (name, canonical_oid(name)), file=sys.stderr)
            out = os.path.join(work, name)
            subprocess.run([exe, "run", "--spec", str(examples / (name + ".json")), "--out", out, "--threads", "1",
                            "--progress", "none"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            prov = json.loads(Path(out, "provenance.json").read_text())
            summary = json.loads(Path(out, "run_summary.json").read_text())
            print("%s\t%s\t%s" % (platform_key(prov), name, summary["oid"]))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(0)

# ---- 1. spec_oid of the canonical form (no Geant4 run, no platform dependence)
recorded = {k[0]: v for k, v in read_table(golden / "SPEC10_OIDS.txt").items()}
for name in NAMES:
    expect(recorded.get(name) == canonical_oid(name),
           "%s.json: spec_oid %s equals the recorded value %s" % (name, canonical_oid(name), recorded.get(name)))
    valid = subprocess.run([exe, "validate", "--spec", str(examples / (name + ".json"))], stdout=subprocess.PIPE,
                           text=True)
    expect(valid.returncode == 0 and json.loads(valid.stdout) == {"event": "valid", "spec_version": "1.0"},
           "%s.json: still valid, and reported as spec_version 1.0" % name)

# ---- 2. OID of hits.arrow on this build
vikshep_testlib.require_data_sets()
runs = read_table(golden / "RUN_OIDS.txt")
work = tempfile.mkdtemp(prefix="vikshep-golden-")
try:
    for name in NAMES:
        out = os.path.join(work, name)
        subprocess.run([exe, "run", "--spec", str(examples / (name + ".json")), "--out", out, "--threads", "1",
                        "--progress", "none"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        prov = json.loads(Path(out, "provenance.json").read_text())
        summary = json.loads(Path(out, "run_summary.json").read_text())
        key = platform_key(prov)
        expect(prov["spec_oid"] == recorded.get(name), "%s: provenance spec_oid equals the recorded spec_oid" % name)
        expect(prov["spec_version"] == "1.0" and "field" not in prov["spec"] and
               summary["spec_version"] == "1.0", "%s: provenance and summary say spec 1.0, no field block" % name)
        want = runs.get((key, name))
        if want is None:
            print("NOTE  %s: no OID recorded for this build (%s); the run OID %s was not compared" %
                  (name, key, summary["oid"]))
        else:
            expect(summary["oid"] == want, "%s: OID of hits.arrow %s equals the recorded %s (%s)" %
                   (name, summary["oid"], want, key))
finally:
    shutil.rmtree(work, ignore_errors=True)

if problems:
    print("\n%d check(s) failed" % len(problems))
    sys.exit(1)
