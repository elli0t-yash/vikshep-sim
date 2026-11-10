#!/usr/bin/env python3
"""Both versions of trajectories.json (docs/EXPORT3D.md sections 3, 6 and 7), without running a simulation.

Usage: check_trajectory_formats.py <tests dir>

  - the format 1 file written by the previous release (v0.1.0) validates against the 1.0 schema and is
    read by the reader that follows the rule of section 6;
  - the format 2 file validates against the 2.0 schema; a format 1 file does not, and the reverse;
  - schema negative cases: a point of three numbers, a missing end_process, a wrong version, an unknown
    key, a cap exceeded;
  - the reader gives x, y, z for both versions and reports whether time, energy, process and
    interactions exist.
"""
import copy
import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator

tests = Path(sys.argv[1])
problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


def read_trajectories(doc):
    """The reader of section 6: accepts 1.0 and 2.0, takes x, y, z from the first three numbers."""
    version = doc["format_version"]
    if version not in ("1.0", "2.0"):
        raise ValueError("unknown format_version " + version)
    rich = version == "2.0"
    out = []
    for e in doc["events"]:
        tracks = [{"track_id": t["track_id"], "xyz": [p[:3] for p in t["points"]],
                   "t_ns": [p[3] for p in t["points"]] if rich else None} for t in e["tracks"]]
        out.append({"event_id": e["event_id"], "tracks": tracks,
                    "interactions": e["interactions"] if rich else None})
    return {"rich": rich, "events": out}


def errors(schema, doc):
    return list(Draft202012Validator(schema).iter_errors(doc))


v1_schema = json.loads((tests / "trajectories-1.0.schema.json").read_text())
v2_schema = json.loads((tests / "trajectories-2.0.schema.json").read_text())
Draft202012Validator.check_schema(v1_schema)
Draft202012Validator.check_schema(v2_schema)
v1 = json.loads((tests / "fixtures" / "trajectories_format1_v0.1.0_tracker_muon.json").read_text())
v2 = json.loads((tests / "fixtures" / "trajectories_format2_teaching_muon_field.json").read_text())

expect(v1["format_version"] == "1.0" and not errors(v1_schema, v1),
       "the format 1 file of release v0.1.0 validates against the 1.0 schema")
expect(v2["format_version"] == "2.0" and not errors(v2_schema, v2),
       "the format 2 file validates against the 2.0 schema")
expect(errors(v2_schema, v1) and errors(v1_schema, v2), "each schema rejects the other version")

r1, r2 = read_trajectories(v1), read_trajectories(v2)
expect(not r1["rich"] and len(r1["events"]) == 10 and r1["events"][0]["tracks"][0]["t_ns"] is None and
       r1["events"][0]["interactions"] is None and len(r1["events"][0]["tracks"][0]["xyz"][0]) == 3,
       "the reader reads format 1: 10 events, x/y/z, no time and no interactions")
expect(r2["rich"] and len(r2["events"]) == 10 and len(r2["events"][0]["tracks"][0]["xyz"][0]) == 3 and
       r2["events"][0]["tracks"][0]["t_ns"][0] == 0.0 and any(e["interactions"] for e in r2["events"]),
       "the reader reads format 2: x/y/z, time starting at 0, interactions present")
bad = copy.deepcopy(v2)
bad["format_version"] = "3.0"
try:
    read_trajectories(bad)
    refused = False
except ValueError:
    refused = True
expect(refused, "the reader refuses an unknown major version")
future = copy.deepcopy(v2)
future["format_version"] = "2.0"
future["new_optional_key"] = 1
future["events"][0]["new_event_key"] = [1]
expect(read_trajectories(future)["rich"], "the reader ignores keys it does not know (section 6)")

# negative cases against the 2.0 schema
def mutated(change):
    doc = copy.deepcopy(v2)
    change(doc)
    return doc


cases = {
    "a point of three numbers": lambda d: d["events"][0]["tracks"][0].__setitem__("points", [[0, 0, 0]]),
    "a missing end_process": lambda d: d["events"][0]["tracks"][0].pop("end_process"),
    "format_version 1.0 with the 2.0 layout": lambda d: d.__setitem__("format_version", "1.0"),
    "an unknown key in an event": lambda d: d["events"][0].__setitem__("extra", 1),
    "a negative time": lambda d: d["events"][0]["tracks"][0]["points"][1].__setitem__(3, -1.0),
    "a non-string process": lambda d: d["events"][0]["tracks"][0]["points"][1].__setitem__(6, 5),
    "more than 2000 interactions": lambda d: d["events"][0].__setitem__(
        "interactions", [{"track_id": 1, "point_index": 1, "t_ns": 0, "position_mm": [0, 0, 0], "process": "x",
                          "secondaries": [], "edep_GeV": 0}] * 2001),
    "a different cap": lambda d: d.__setitem__("max_interactions_per_event", 10),
}
for name, change in cases.items():
    expect(bool(errors(v2_schema, mutated(change))), "the 2.0 schema rejects " + name)

if problems:
    print("\n%d check(s) failed" % len(problems))
    sys.exit(1)
print("\nall format checks passed")
