#!/usr/bin/env python3
"""Checks docs/spec.schema.json against the C++ validator.

For every spec below, the JSON Schema and `vikshep-sim validate` must agree,
except for the cases that depend on names from the installed Geant4 or on the
world size, which the schema cannot express: the schema accepts them, the
validator rejects them.

Usage: check_schema.py <vikshep-sim> <schema.json> <examples dir>
"""
import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator

exe, schema_path, examples_dir = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])

schema = json.loads(schema_path.read_text())
Draft202012Validator.check_schema(schema)
schema_validator = Draft202012Validator(schema)
base = json.loads((examples_dir / "calorimeter.json").read_text())

DEL = object()


def edit(spec, pointer, value):
    spec = copy.deepcopy(spec)
    parts = [p.replace("~1", "/").replace("~0", "~") for p in pointer.strip("/").split("/")]
    node = spec
    for part in parts[:-1]:
        node = node[int(part)] if isinstance(node, list) else node.setdefault(part, {})
    last = int(parts[-1]) if isinstance(node, list) else parts[-1]
    if value is DEL:
        del node[last]
    else:
        node[last] = value
    return spec


# (name, spec, expected) where expected is "valid", "invalid" (both reject) or
# "catalogue" (schema accepts, validator rejects: needs Geant4 names or the
# world size, which the schema cannot express).
cases = [("example", base, "valid")]
for f in sorted(examples_dir.glob("*.json")):
    cases.append((f.name, json.loads(f.read_text()), "valid"))

valid_edits = [
    ("explicit defaults", [("/gun/position_mm", [0, 0, -85]), ("/gun/direction", [0, 0, 1]),
                           ("/physics", {"list": "FTFP_BERT"}), ("/sensitive", ["active"])]),
    ("both sensitive", [("/sensitive", ["absorber", "active"])]),
    ("empty physics", [("/physics", {})]),
    ("integer-valued float", [("/events", 20.0), ("/geometry/layers", 10.0), ("/seed", 42.0)]),
    ("max layers", [("/geometry/layers", 200)]),
    ("max energy", [("/gun/energy_GeV", 10000)]),
    ("max events", [("/events", 10000000)]),
    ("max seed", [("/seed", 9007199254740991)]),
    ("tiny thickness", [("/geometry/active/thickness_mm", 1e-9)]),
    ("unnormalised direction", [("/gun/direction", [0, 3, 4])]),
    ("negative position", [("/gun/position_mm", [-1, -2, -170])]),
    ("other particle", [("/gun/particle", "kaon+")]),
]
for name, edits in valid_edits:
    spec = base
    for pointer, value in edits:
        spec = edit(spec, pointer, value)
    cases.append((name, spec, "valid"))

for parent, keys in [("", ["spec_version", "template", "geometry", "gun", "events", "seed"]),
                     ("/geometry", ["layers", "absorber", "active", "transverse_mm"]),
                     ("/geometry/absorber", ["material", "thickness_mm"]),
                     ("/geometry/active", ["material", "thickness_mm"]),
                     ("/gun", ["particle", "energy_GeV"])]:
    for key in keys:
        cases.append(("missing %s/%s" % (parent, key), edit(base, parent + "/" + key, DEL), "invalid"))

invalid_edits = [
    ("spec_version number", "/spec_version", 1.0),
    ("template other", "/template", "tracker"),
    ("geometry string", "/geometry", "x"),
    ("layers string", "/geometry/layers", "10"),
    ("layers fraction", "/geometry/layers", 10.5),
    ("layers zero", "/geometry/layers", 0),
    ("layers 201", "/geometry/layers", 201),
    ("material number", "/geometry/absorber/material", 5),
    ("material empty", "/geometry/absorber/material", ""),
    ("thickness zero", "/geometry/absorber/thickness_mm", 0),
    ("thickness negative", "/geometry/active/thickness_mm", -1),
    ("thickness string", "/geometry/active/thickness_mm", "5"),
    ("transverse zero", "/geometry/transverse_mm", 0),
    ("particle number", "/gun/particle", 7),
    ("particle unknown", "/gun/particle", "electron"),
    ("energy zero", "/gun/energy_GeV", 0),
    ("energy too high", "/gun/energy_GeV", 10000.5),
    ("energy string", "/gun/energy_GeV", "5"),
    ("position short", "/gun/position_mm", [0, 0]),
    ("position strings", "/gun/position_mm", ["a", 0, 0]),
    ("direction zero", "/gun/direction", [0, 0, 0]),
    ("direction float zero", "/gun/direction", [0.0, 0.0, 0.0]),
    ("direction long", "/gun/direction", [0, 0, 1, 1]),
    ("physics number", "/physics", 5),
    ("physics list number", "/physics", {"list": 3}),
    ("sensitive string", "/sensitive", "active"),
    ("sensitive empty", "/sensitive", []),
    ("sensitive duplicate", "/sensitive", ["active", "active"]),
    ("sensitive unknown", "/sensitive", ["gap"]),
    ("events zero", "/events", 0),
    ("events too many", "/events", 10000001),
    ("events string", "/events", "20"),
    ("events fraction", "/events", 20.5),
    ("seed negative", "/seed", -1),
    ("seed too big", "/seed", 9007199254740992),
    ("seed null", "/seed", None),
    ("unknown top-level", "/extra", 1),
    ("unknown in geometry", "/geometry/depth", 1),
    ("unknown in slab", "/geometry/absorber/thickness", 1),
    ("unknown in gun", "/gun/energy", 5),
    ("unknown in physics", "/physics/lst", "QBBC"),
]
for name, pointer, value in invalid_edits:
    cases.append((name, edit(base, pointer, value), "invalid"))

cases.append(("root array", [], "invalid"))
cases.append(("bad material", edit(base, "/geometry/absorber/material", "G4_Pbb"), "catalogue"))
cases.append(("gun outside world z", edit(base, "/gun/position_mm", [0, 0, -1000]), "catalogue"))
cases.append(("gun outside world x", edit(base, "/gun/position_mm", [150, 0, 0]), "catalogue"))
cases.append(("gun on world boundary", edit(base, "/gun/position_mm", [0, 0, 175]), "catalogue"))
cases.append(("gun inside world", edit(base, "/gun/position_mm", [149, -149, 174]), "valid"))
cases.append(("bad physics list", edit(base, "/physics/list", "NOT_A_LIST"), "catalogue"))

# ---- spec 1.1: tracker_barrel and field
tracker = json.loads((examples_dir / "tracker_muon.json").read_text())


def many_layers(n):
    return [{"radius_mm": 10.0 + 10.0 * i, "thickness_mm": 1, "half_length_mm": 100} for i in range(n)]


def build(spec, edits):
    for pointer, value in edits:
        spec = edit(spec, pointer, value)
    return spec


tracker_valid = [
    ("tracker explicit defaults", [("/gun/position_mm", [0, 0, 0]), ("/gun/direction", [1, 0, 0]),
                                   ("/sensitive", ["tracker_layer"]), ("/field", {"B_T": [0, 0, 0]}),
                                   ("/geometry/layers/0/material", "G4_Si")]),
    ("tracker empty field", [("/field", {})]),
    ("tracker no field", [("/field", DEL)]),
    ("tracker other material", [("/geometry/layers/2/material", "G4_Ge")]),
    ("tracker 100 layers", [("/geometry/layers", many_layers(100))]),
    ("tracker single layer", [("/geometry/layers", many_layers(1))]),
    ("tracker field along x", [("/field/B_T", [3, 0, 0])]),
    ("tracker integer-valued floats", [("/geometry/layers/0/radius_mm", 50.0), ("/events", 50.0)]),
    ("tracker limits", [("/geometry/layers/5/radius_mm", 10000), ("/geometry/layers/5/half_length_mm", 10000)]),
    ("calorimeter as 1.1", [("/spec_version", "1.1")]),
]
for name, edits in tracker_valid:
    cases.append((name, build(tracker, edits), "valid"))
cases.append(("calorimeter as 1.1 with field", build(base, [("/spec_version", "1.1"), ("/field", {"B_T": [0, 0, 1]})]),
              "valid"))

tracker_invalid = [
    ("tracker in 1.0", [("/spec_version", "1.0")]),
    ("version 1.2", [("/spec_version", "1.2")]),
    ("version 1", [("/spec_version", "1")]),
    ("template misspelt", [("/template", "tracker_barel")]),
    ("field B_T short", [("/field/B_T", [0, 2])]),
    ("field B_T strings", [("/field/B_T", ["0", "0", "2"])]),
    ("field B_T number", [("/field/B_T", 2)]),
    ("field unknown key", [("/field/Bz_T", 2)]),
    ("field array", [("/field", [0, 0, 2])]),
    ("layers empty", [("/geometry/layers", [])]),
    ("layers 101", [("/geometry/layers", many_layers(101))]),
    ("layers integer", [("/geometry/layers", 6)]),
    ("layer not object", [("/geometry/layers/1", 100)]),
    ("layer radius missing", [("/geometry/layers/1/radius_mm", DEL)]),
    ("layer thickness missing", [("/geometry/layers/1/thickness_mm", DEL)]),
    ("layer half-length missing", [("/geometry/layers/1/half_length_mm", DEL)]),
    ("layer radius zero", [("/geometry/layers/1/radius_mm", 0)]),
    ("layer radius too large", [("/geometry/layers/1/radius_mm", 10000.5)]),
    ("layer radius string", [("/geometry/layers/1/radius_mm", "100")]),
    ("layer thickness zero", [("/geometry/layers/1/thickness_mm", 0)]),
    ("layer half-length zero", [("/geometry/layers/1/half_length_mm", 0)]),
    ("layer half-length too large", [("/geometry/layers/1/half_length_mm", 10000.5)]),
    ("layer material number", [("/geometry/layers/1/material", 14)]),
    ("layer material empty", [("/geometry/layers/1/material", "")]),
    ("layer unknown key", [("/geometry/layers/1/radius", 100)]),
    ("tracker with transverse_mm", [("/geometry/transverse_mm", 100)]),
    ("tracker with absorber", [("/geometry/absorber", {"material": "G4_Pb", "thickness_mm": 1})]),
    ("tracker sensitive active", [("/sensitive", ["active"])]),
    ("tracker sensitive duplicate", [("/sensitive", ["tracker_layer", "tracker_layer"])]),
    ("tracker sensitive empty", [("/sensitive", [])]),
]
for name, edits in tracker_invalid:
    cases.append((name, build(tracker, edits), "invalid"))
cases.append(("field in 1.0", edit(base, "/field", {"B_T": [0, 0, 1]}), "invalid"))
cases.append(("calorimeter 1.1 with layers array", build(base, [("/spec_version", "1.1"), ("/geometry/layers", [])]),
              "invalid"))
cases.append(("calorimeter sensitive tracker_layer", edit(base, "/sensitive", ["tracker_layer"]), "invalid"))

# the schema cannot compare array elements, use the world, or know the names of Geant4
tracker_catalogue = [
    ("layer thickness not below radius", [("/geometry/layers/1/thickness_mm", 100)]),
    ("layer radius equal to previous", [("/geometry/layers/2/radius_mm", 100)]),
    ("layer radius below previous", [("/geometry/layers/2/radius_mm", 60)]),
    ("layer overlapping previous", [("/geometry/layers/1/radius_mm", 50.2)]),
    ("field too strong", [("/field/B_T", [0, 0, 10.5])]),
    ("layer bad material", [("/geometry/layers/2/material", "G4_Sii")]),
    ("tracker gun outside world", [("/gun/position_mm", [0, 0, 700])]),
]
for name, edits in tracker_catalogue:
    cases.append((name, build(tracker, edits), "catalogue"))
cases.append(("tracker gun inside world", build(tracker, [("/gun/position_mm", [399, 0, 599])]), "valid"))

failures = []
with tempfile.TemporaryDirectory() as tmp:
    for i, (name, spec, expected) in enumerate(cases):
        path = Path(tmp) / ("case%d.json" % i)
        path.write_text(json.dumps(spec))
        schema_ok = not list(schema_validator.iter_errors(spec))
        proc = subprocess.run([exe, "validate", "--spec", str(path)],
                              capture_output=True, text=True)
        cli_ok = proc.returncode == 0
        want_schema = expected in ("valid", "catalogue")
        want_cli = expected == "valid"
        if schema_ok != want_schema or cli_ok != want_cli:
            failures.append("%s: expected %s, schema_ok=%s cli_ok=%s" % (name, expected, schema_ok, cli_ok))
        if proc.returncode not in (0, 2):
            failures.append("%s: unexpected exit code %d" % (name, proc.returncode))

print("%d cases checked" % len(cases))
if failures:
    print("\n".join(failures))
    sys.exit(1)
print("schema and validator agree")
