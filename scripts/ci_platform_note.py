#!/usr/bin/env python3
"""Markdown for the CI job summary: the OID of the example run on this platform, with the note that
OIDs of different platforms are expected to differ.

    python scripts/ci_platform_note.py <run folder> [job name] >> "$GITHUB_STEP_SUMMARY"
"""
import json
import sys
from pathlib import Path

run = Path(sys.argv[1])
job = sys.argv[2] if len(sys.argv) > 2 else ""
summary = json.loads((run / "run_summary.json").read_text())
prov = json.loads((run / "provenance.json").read_text())
spec = prov["spec"]

print("### Example run%s" % (" (%s)" % job if job else ""))
print()
print("`docs/examples/calorimeter.json` (%s %g GeV, %d events, seed %d), run on **%s %s**, %s, Geant4 %s, build type %s:" % (
    spec["gun"]["particle"], spec["gun"]["energy_GeV"], spec["events"], spec["seed"], prov["platform"]["os"],
    prov["platform"]["arch"], prov["platform"]["compiler"], prov["geant4"]["version"], prov["build_type"]))
print()
print("| OID | hits | events with hits | file bytes |")
print("|-----|-----:|-----------------:|-----------:|")
print("| `%s` | %d | %d | %d |" % (summary["oid"], summary["hits_total"], summary["events_with_hits"], summary["hits_file_bytes"]))
print()
print("Different operating systems, compilers and builds are expected to give **different OIDs** for the same "
      "spec and seed, and that is not a failure: the promise is the same machine, the same build and the same "
      "seed. OIDs are never compared across platforms.")
