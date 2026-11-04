#!/usr/bin/env python3
"""Determinism of tracker and field runs, and the rule that a zero field is no field
(docs/SPEC.md, 2.6 and 2.7).

Usage: check_tracker_determinism.py <vikshep-sim> <examples dir>
Exit code 77 (ctest: skipped) when the Geant4 data sets are missing.
"""
import copy
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tracker_lib as tl  # noqa: E402
import vikshep_testlib  # noqa: E402

exe, examples = sys.argv[1], Path(sys.argv[2])
vikshep_testlib.require_data_sets()

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


work = tempfile.mkdtemp(prefix="vikshep-trdet-")
counter = [0]


def simulate(spec, threads=1):
    counter[0] += 1
    out = os.path.join(work, "run%d" % counter[0])
    summary, provenance = tl.run(exe, spec, out, threads)
    hits = Path(out, "hits.arrow").read_bytes()
    return {"oid": summary["oid"], "spec_oid": provenance["spec_oid"], "hits": hits,
            "provenance": Path(out, "provenance.json").read_bytes(), "prov": provenance,
            "sha": hashlib.sha3_256(hits).hexdigest()[:28]}


def same_bytes(a, b):
    return a["hits"] == b["hits"] and a["oid"] == b["oid"]


try:
    # ---- tracker, B = 2 T: same spec and seed, run twice, 1 and 4 threads
    spec = tl.tracker_spec(field_t=(0, 0, 2), events=60)
    first = simulate(spec, 1)
    second = simulate(spec, 1)
    four = simulate(spec, 4)
    print("tracker, B = 2 T, 60 events, seed 42: OID %s (%d bytes)" % (first["oid"], len(first["hits"])))
    expect(first["oid"] == first["sha"], "the OID is the SHA3-256 prefix of the bytes of hits.arrow")
    expect(same_bytes(first, second), "tracker + field: run twice with 1 thread: identical hits.arrow (OID %s)" %
           second["oid"])
    expect(same_bytes(first, four), "tracker + field: 1 and 4 threads: identical hits.arrow (OID %s)" % four["oid"])
    expect(first["provenance"] == second["provenance"] == four["provenance"],
           "tracker + field: provenance.json identical for the three runs")
    other_seed = simulate(dict(spec, seed=43), 1)
    expect(other_seed["oid"] != first["oid"], "a different seed gives a different OID (%s)" % other_seed["oid"])

    # ---- the field is part of the canonical spec, hence of spec_oid, and changes the hits
    stronger = simulate(tl.tracker_spec(field_t=(0, 0, 2.0001), events=60), 1)
    expect(stronger["spec_oid"] != first["spec_oid"],
           "B_T = [0,0,2.0001] has another spec_oid than [0,0,2] (%s, %s)" % (stronger["spec_oid"], first["spec_oid"]))
    expect(stronger["oid"] != first["oid"], "and another OID of hits.arrow")
    expect(first["prov"]["spec"]["field"] == {"B_T": [0, 0, 2]}, "provenance.json: spec.field is {B_T: [0, 0, 2]}")

    # ---- a zero field is no field: with and without the block, and with -0
    no_block = simulate(tl.tracker_spec(field_t=None, events=60), 1)
    zero = simulate(tl.tracker_spec(field_t=(0, 0, 0), events=60), 1)
    negative_zero = simulate(tl.tracker_spec(field_t=(-0.0, 0.0, -0.0), events=60), 1)
    print("tracker, zero field: OID %s" % zero["oid"])
    expect(same_bytes(no_block, zero) and same_bytes(zero, negative_zero),
           "tracker: no field block, [0,0,0] and [-0,0,-0] give identical hits.arrow")
    expect(no_block["spec_oid"] == zero["spec_oid"] == negative_zero["spec_oid"],
           "and the same spec_oid (%s)" % zero["spec_oid"])
    expect(zero["oid"] != first["oid"], "and not the OID of the 2 T run")

    # ---- a spec written as 1.0 and the same experiment written as 1.1 with a zero field
    cal10 = json.loads((examples / "calorimeter.json").read_text())
    cal11 = copy.deepcopy(cal10)
    cal11["spec_version"] = "1.1"
    cal11["field"] = {"B_T": [0, 0, 0]}
    r10 = simulate(cal10, 1)
    r11 = simulate(cal11, 1)
    print("calorimeter.json: as 1.0 OID %s, as 1.1 with zero field OID %s" % (r10["oid"], r11["oid"]))
    expect(same_bytes(r10, r11), "calorimeter written as 1.0 and as 1.1 with a zero field: identical hits.arrow")
    expect(r10["spec_oid"] != r11["spec_oid"],
           "but different spec_oid (the version string is part of the canonical bytes): %s vs %s" %
           (r10["spec_oid"], r11["spec_oid"]))
    expect("field" not in r10["prov"]["spec"] and r11["prov"]["spec"]["field"] == {"B_T": [0, 0, 0]},
           "provenance.json: no field key for 1.0, field {B_T: [0,0,0]} for 1.1")

    # ---- the calorimeter in a field: deterministic, and the field changes the hits
    cal_field = copy.deepcopy(cal11)
    cal_field["field"] = {"B_T": [0, 0, 1]}
    c1 = simulate(cal_field, 1)
    c4 = simulate(cal_field, 4)
    expect(same_bytes(c1, c4), "calorimeter in B = 1 T along z: 1 and 4 threads identical (OID %s)" % c1["oid"])
    expect(c1["oid"] != r11["oid"], "and different from the zero-field run")
finally:
    shutil.rmtree(work, ignore_errors=True)

if problems:
    print("\n%d check(s) failed" % len(problems))
    sys.exit(1)
print("\nall determinism checks passed")
