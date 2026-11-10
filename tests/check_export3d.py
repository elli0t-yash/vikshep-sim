#!/usr/bin/env python3
"""The 3-D export of `vikshep-sim run --export-3d` (docs/EXPORT3D.md), end to end, with the numbers printed.

Usage: check_export3d.py <vikshep-sim> <examples dir> <golden dir> <trajectories-2.0.schema.json>
Exit code 77 (ctest: skipped) when the Geant4 data sets are missing.
"""
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from jsonschema import Draft202012Validator

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tracker_lib as tl  # noqa: E402
import vikshep_testlib  # noqa: E402

exe, examples, golden, schema_path = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4])
vikshep_testlib.require_data_sets()

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


work = tempfile.mkdtemp(prefix="vikshep-export-")
counter = [0]


def cli(spec_path, extra=(), threads=1, env=None, progress="none"):
    """Runs `vikshep-sim run`; returns (CompletedProcess, out dir)."""
    counter[0] += 1
    out = os.path.join(work, "run%d" % counter[0])
    environment = dict(os.environ, **(env or {}))
    proc = subprocess.run([exe, "run", "--spec", str(spec_path), "--out", out, "--threads", str(threads),
                           "--progress", progress, *extra], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, env=environment)
    return proc, out


def write_spec(spec):
    counter[0] += 1
    path = os.path.join(work, "spec%d.json" % counter[0])
    Path(path).write_text(json.dumps(spec))
    return path


def simulate(spec_path, extra=(), threads=1):
    proc, out = cli(spec_path, extra, threads)
    if proc.returncode != 0:
        raise RuntimeError("run failed (%d): %s" % (proc.returncode, proc.stderr[-600:]))
    return out


def read(out, name):
    return Path(out, name).read_bytes()


def oid(data):
    return hashlib.sha3_256(data).hexdigest()[:28]


# ------------------------------------------------------------------ an independent glb parser
def parse_glb(data):
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF" and version == 2 and length == len(data), "glb header"
    jlen, jtype = struct.unpack_from("<I4s", data, 12)
    assert jtype == b"JSON" and jlen % 4 == 0, "json chunk"
    doc = json.loads(data[20:20 + jlen].decode("utf-8"))
    at = 20 + jlen
    blen, btype = struct.unpack_from("<I4s", data, at)
    assert btype == b"BIN\x00" and blen % 4 == 0 and at + 8 + blen == len(data), "bin chunk"
    return doc, data[at + 8:at + 8 + blen]


SIZES = {5126: 4, 5123: 2}
WIDTH = {"SCALAR": 1, "VEC3": 3}


def accessor_array(doc, binary, index):
    a = doc["accessors"][index]
    view = doc["bufferViews"][a["bufferView"]]
    assert view["buffer"] == 0 and view["byteOffset"] % 4 == 0
    n = a["count"] * WIDTH[a["type"]]
    assert view["byteOffset"] + n * SIZES[a["componentType"]] <= len(binary), "accessor inside the buffer"
    assert n * SIZES[a["componentType"]] <= view["byteLength"], "accessor inside its view"
    dtype = "<f4" if a["componentType"] == 5126 else "<u2"
    arr = np.frombuffer(binary, dtype=dtype, count=n, offset=view["byteOffset"])
    return arr.reshape(-1, 3) if a["type"] == "VEC3" else arr


def check_glb(label, glb, spec_resolved, expect_nodes, template):
    doc, binary = parse_glb(glb)
    expect(True, "%s: header, JSON chunk and BIN chunk parse; length %d bytes" % (label, len(glb)))
    expect(doc["buffers"][0]["byteLength"] == len(binary) and len(doc["buffers"]) == 1,
           "%s: one buffer whose length is the BIN chunk's" % label)
    expect(len(doc["nodes"]) == expect_nodes and doc["scenes"][0]["nodes"] == list(range(expect_nodes)),
           "%s: node count %d equals the volume count %d" % (label, len(doc["nodes"]), expect_nodes))
    ex = doc["asset"]["extras"]
    expect(ex["length_unit"] == "m" and ex["mm_per_unit"] == 1000 and ex["beam_axis"] == "+z" and
           ex["frame"] == "geant4_global" and ex["template"] == template and
           ex["spec_oid"] == spec_resolved["spec_oid"], "%s: asset extras (unit, beam axis, template, spec_oid)" % label)
    bad = []
    triangles = 0
    for mi, mesh in enumerate(doc["meshes"]):
        prim = mesh["primitives"][0]
        pos = accessor_array(doc, binary, prim["attributes"]["POSITION"])
        nrm = accessor_array(doc, binary, prim["attributes"]["NORMAL"])
        idx = accessor_array(doc, binary, prim["indices"])
        acc = doc["accessors"][prim["attributes"]["POSITION"]]
        if not (np.float32(acc["min"]) == pos.min(axis=0)).all() or not (np.float32(acc["max"]) == pos.max(axis=0)).all():
            bad.append("%s min/max" % mesh["name"])
        if idx.max() >= len(pos) or len(idx) % 3:
            bad.append("%s indices" % mesh["name"])
        if not np.allclose(np.linalg.norm(nrm, axis=1), 1.0, atol=1e-5):
            bad.append("%s normals not unit" % mesh["name"])
        tri = idx.reshape(-1, 3).astype(int)
        a, b, c = pos[tri[:, 0]].astype(float), pos[tri[:, 1]].astype(float), pos[tri[:, 2]].astype(float)
        face = np.cross(b - a, c - a)
        if not (np.einsum("ij,ij->i", face, nrm[tri[:, 0]].astype(float)) > 0).all():
            bad.append("%s: a triangle is wound clockwise seen from outside" % mesh["name"])
        triangles += len(tri)
    expect(not bad, "%s: accessor bounds, index range, unit normals, counter-clockwise winding on %d triangles%s" %
           (label, triangles, "" if not bad else " " + str(bad)))
    return doc, binary


# ------------------------------------------------------------------ golden OIDs (this build only)
def platform_key(provenance):
    p = provenance["platform"]
    return "%s %s %s | geant4 %s %s | %s" % (p["os"], p["arch"], p["compiler"], provenance["geant4"]["version"],
                                              provenance["geant4_conda"]["build"], provenance["build_type"])


recorded = {}
for line in (golden / "RUN_OIDS.txt").read_text().splitlines():
    if line.strip():
        key, name, value = line.split("\t")
        recorded[(key, name)] = value

try:
    # ================================================================== the tracker muon example
    muon_spec = examples / "tracker_muon.json"
    runs = {}
    for flag in ("off", "on"):
        for threads in (1, 4):
            extra = ["--export-3d"] if flag == "on" else []
            runs[(flag, threads)] = simulate(muon_spec, extra, threads)
    hits = {k: read(v, "hits.arrow") for k, v in runs.items()}
    oids = {k: json.loads(read(v, "run_summary.json"))["oid"] for k, v in runs.items()}
    print("tracker_muon.json (50 events, seed 42): OID of hits.arrow")
    for k in sorted(oids):
        print("   export %-3s %d thread(s): %s" % (k[0], k[1], oids[k]))
    expect(len(set(hits.values())) == 1 and len(set(oids.values())) == 1,
           "tracker_muon: hits.arrow is byte-identical with the export on and off, with 1 and 4 threads (OID %s)" %
           oids[("off", 1)])
    expect(oids[("off", 1)] == hashlib.sha3_256(hits[("off", 1)]).hexdigest()[:28], "and it is the SHA3-256 prefix of the bytes")
    prov_off = json.loads(read(runs[("off", 1)], "provenance.json"))
    key = platform_key(prov_off)
    want = recorded.get((key, "tracker_muon"))
    if want is None:
        print("NOTE  no OID recorded for tracker_muon on this build (%s); not compared" % key)
    else:
        expect(oids[("off", 1)] == want, "tracker_muon: OID %s equals the value recorded for this build" % want)

    # nothing new without the flag
    expect(sorted(os.listdir(runs[("off", 1)])) == ["hits.arrow", "provenance.json", "run_summary.json"],
           "without --export-3d the folder has exactly hits.arrow, provenance.json, run_summary.json")
    expect("export_3d" not in prov_off and "exports" not in json.loads(read(runs[("off", 1)], "run_summary.json")),
           "and neither provenance.json nor run_summary.json mention the export")
    expect(sorted(os.listdir(runs[("on", 1)])) ==
           ["geometry.glb", "hits.arrow", "provenance.json", "run_summary.json", "trajectories.json"],
           "with --export-3d the folder has the two extra files")

    # same seed, same bytes of both files and of provenance, 1 and 4 threads, and a second run
    again = simulate(muon_spec, ["--export-3d"], 1)
    same = all(read(runs[("on", 1)], n) == read(runs[("on", 4)], n) == read(again, n)
               for n in ("geometry.glb", "trajectories.json", "provenance.json"))
    expect(same, "tracker_muon: geometry.glb, trajectories.json and provenance.json are byte-identical for 1 thread, "
           "4 threads and a repeated run")
    summary_on = json.loads(read(runs[("on", 1)], "run_summary.json"))
    prov_on = json.loads(read(runs[("on", 1)], "provenance.json"))
    sizes = {n: len(read(runs[("on", 1)], n)) for n in ("geometry.glb", "trajectories.json")}
    expect([e["file"] for e in summary_on["exports"]] == ["geometry.glb", "trajectories.json"] and
           all(e["bytes"] == sizes[e["file"]] and e["oid"] == oid(read(runs[("on", 1)], e["file"]))
               for e in summary_on["exports"]), "run_summary.json lists both files with the right sizes and OIDs")
    expect(prov_on["export_3d"]["files"] == summary_on["exports"] and prov_on["export_3d"]["events"] == 10 and
           prov_on["export_3d"]["max_tracks_per_event"] == 500 and prov_on["export_3d"]["min_energy_GeV"] == 0.001 and
           prov_on["export_3d"]["format_version"] == "2.0" and prov_on["export_3d"]["max_interactions_per_event"] == 2000,
           "provenance.json export_3d: the same files, 10 events, the limits")
    expect(all(e["oid"] != oids[("on", 1)] for e in summary_on["exports"]), "the export OIDs are not the OID of the hits")
    print("   sizes with 10 events: geometry.glb %d bytes, trajectories.json %d bytes" %
          (sizes["geometry.glb"], sizes["trajectories.json"]))

    # the done line (jsonl)
    proc, out = cli(muon_spec, ["--export-3d"], 1, progress="jsonl")
    lines = [json.loads(line) for line in proc.stdout.splitlines()]
    done = lines[-1]
    expect(proc.returncode == 0 and done["event"] == "done" and list(done.keys()) == sorted(done.keys()) and
           done["exports"] == [{"bytes": sizes["geometry.glb"], "file": "geometry.glb"},
                               {"bytes": sizes["trajectories.json"], "file": "trajectories.json"}] and
           done["oid"] == oids[("off", 1)], "the done line lists the exported files and sizes; oid is the hits OID")
    proc, out = cli(muon_spec, [], 1, progress="jsonl")
    expect(json.loads(proc.stdout.splitlines()[-1]) == {"event": "done", "oid": oids[("off", 1)]},
           "without the flag the done line is exactly {event, oid}")

    # geometry.glb of the tracker
    spec_resolved = {"spec_oid": prov_on["spec_oid"]}
    doc, binary = check_glb("tracker glb", read(runs[("on", 1)], "geometry.glb"), spec_resolved, 6, "tracker_barrel")
    layers = prov_on["spec"]["geometry"]["layers"]
    ok = True
    for i, mesh in enumerate(doc["meshes"]):
        pos = accessor_array(doc, binary, mesh["primitives"][0]["attributes"]["POSITION"]).astype(float) * 1000.0
        r = np.hypot(pos[:, 0], pos[:, 1])
        L = layers[i]
        rin, rout = L["radius_mm"] - 0.5 * L["thickness_mm"], L["radius_mm"] + 0.5 * L["thickness_mm"]
        ok = ok and set(np.round(r, 3)) <= {round(rin, 3), round(rout, 3)} and \
            np.allclose(np.abs(pos[:, 2]), L["half_length_mm"], atol=1e-3)
        ok = ok and len(pos) == 512 and doc["nodes"][i]["extras"] == {"copy_no": i, "role": "tracker", "sensitive": True} \
            and doc["nodes"][i]["name"] == "tracker_layer"
    expect(ok, "tracker glb: every vertex is on the inner or outer radius (mm) and at +-half_length; 512 vertices; "
           "node names and extras are right")

    # trajectories.json of the muon run
    text = read(runs[("on", 1)], "trajectories.json").decode()
    traj = json.loads(text)
    schema = json.loads(schema_path.read_text())
    errors = list(Draft202012Validator(schema).iter_errors(traj))
    expect(not errors, "trajectories.json validates against the schema%s" % ("" if not errors else ": " + errors[0].message))
    first = json.loads(text, object_pairs_hook=list)
    top_keys = [k for k, _ in first]
    event_keys = [k for k, _ in first[-1][1][0]]
    expect(top_keys == ["format_version", "unit", "spec_oid", "min_energy_GeV", "max_tracks_per_event",
                        "max_interactions_per_event", "events_requested", "point_fields", "events"] and
           event_keys == ["event_id", "tracks_total", "dropped_tracks", "interactions_total", "dropped_interactions",
                          "tracks", "interactions"] and
           [k for k, _ in first[-1][1][0][-2][1][0]] ==
           ["track_id", "parent_id", "pdg", "particle", "charge", "creator_process", "end_process",
            "kinetic_energy_start_GeV", "points"] and
           all([k for k, _ in i] == ["track_id", "point_index", "t_ns", "position_mm", "process", "secondaries",
                                     "edep_GeV"] for ev in first[-1][1] for i in dict(ev)["interactions"]),
           "keys appear in the documented order")
    body_lines = text.split("\n")
    expect(len(body_lines) == 1 + 10 + 1 + 1 and body_lines[0].endswith('"events":[') and body_lines[-2] == "]}" and
           body_lines[-1] == "", "layout: one header line, one line per event (10), then ]} and a final newline")
    rows = re.findall(r'\[(-?\d+\.\d{3}),(-?\d+\.\d{3}),(-?\d+\.\d{3}),(\d+\.\d{6}),(\d+\.\d{7}),(\d+\.\d{7}),'
                      r'(null|"[A-Za-z0-9_]+")\]', text)
    n_points = sum(len(t["points"]) for e in traj["events"] for t in e["tracks"])
    expect(len(rows) == n_points and n_points > 50 and "-0.000" not in text and not re.search(r"[0-9][eE][+-]?[0-9]", text),
           "every point is [x,y,z] with 3 decimals, t with 6, energy and deposit with 7, and a process name or null "
           "(%d points, all match), no -0.000, no exponent form" % n_points)
    expect(all(re.fullmatch(r"\d+\.\d{6}", e) for e in re.findall(r'"kinetic_energy_start_GeV":([^,]+),', text)) and
           '"min_energy_GeV":0.001000,' in text, "energies are fixed-point with 6 decimals")
    expect(traj["spec_oid"] == prov_on["spec_oid"] and traj["unit"] == "mm", "spec_oid and unit match the run")
    expect([e["event_id"] for e in traj["events"]] == list(range(10)), "the exported events are ids 0 to 9, in order")
    expect(all(e["dropped_tracks"] == e["tracks_total"] - len(e["tracks"]) and
               [t["track_id"] for t in e["tracks"]] == sorted(t["track_id"] for t in e["tracks"]) for e in traj["events"]),
           "dropped_tracks = tracks_total - kept, and tracks are in track_id order")
    prim = [[t for t in e["tracks"] if t["parent_id"] == 0] for e in traj["events"]]
    expect(all(len(p) == 1 and p[0]["pdg"] == 13 and p[0]["charge"] == -1 and p[0]["kinetic_energy_start_GeV"] == 1.0 and
               p[0]["points"][0][:3] == [0.0, 0.0, 0.0] and p[0]["points"][0][6] is None and len(p[0]["points"]) >= 7 for p in prim),
           "each exported event has one primary: pdg 13, charge -1, 1.000000 GeV, starting at the origin, with a "
           "polyline of steps")
    tracks_exported = sum(len(e["tracks"]) for e in traj["events"])
    tracks_total = sum(e["tracks_total"] for e in traj["events"])
    print("   muon run, 10 events: %d tracks exported of %d created (%d dropped)" %
          (tracks_exported, tracks_total, tracks_total - tracks_exported))

    # every hit's track is in the file when nothing is filtered (min energy 0)
    all_dir = simulate(muon_spec, ["--export-3d", "--export-min-energy", "0", "--export-events", "50"], 4)
    t0 = json.loads(read(all_dir, "trajectories.json"))
    h = tl.read_hits(all_dir)
    covered = all(set(h["track_id"][h["event_id"] == e["event_id"]].tolist()) <= {t["track_id"] for t in e["tracks"]}
                  for e in t0["events"])
    expect(covered and all(e["dropped_tracks"] == 0 for e in t0["events"]) and t0["min_energy_GeV"] == 0.0 and
           len(t0["events"]) == 50,
           "--export-min-energy 0 --export-events 50: nothing dropped and every track that has a hit is in the file")
    expect(read(all_dir, "hits.arrow") == hits[("off", 1)], "and hits.arrow is still the same bytes")

    # ---------------------------------------------------------------- radius from trajectories vs from hits
    fit_spec = write_spec(tl.tracker_spec(field_t=(0, 0, 2), events=100))
    fit_dir = simulate(fit_spec, ["--export-3d", "--export-events", "100"], 4)
    off_dir = simulate(fit_spec, [], 1)
    expect(read(fit_dir, "hits.arrow") == read(off_dir, "hits.arrow"), "radius test run: hits.arrow identical with export on (4 threads) and off (1 thread)")
    tfit = json.loads(read(fit_dir, "trajectories.json"))
    hh = tl.read_hits(fit_dir)
    r_traj, r_hits = [], []
    for e in tfit["events"]:
        p = next(t for t in e["tracks"] if t["parent_id"] == 0)["points"]
        xy = np.array([q[:3] for q in p])
        r_traj.append(tl.fit_circle(xy[:, 0], xy[:, 1])[2])
        sel = (hh["event_id"] == e["event_id"]) & (hh["track_id"] == 1)
        r_hits.append(tl.fit_circle(hh["x"][sel], hh["y"][sel])[2])
    r_traj, r_hits = np.array(r_traj) / 1000.0, np.array(r_hits) / 1000.0
    pt = tl.expected_momentum_mev(1.0) / 1000.0
    r_expected = pt / (tl.C_GEV_PER_T_M * 2.0)
    rel = abs(r_traj.mean() / r_hits.mean() - 1.0)
    per_event = np.abs(r_traj / r_hits - 1.0)
    print("   2 T muon, 100 events: R from trajectory points %.4f +- %.4f m, from hits.arrow %.4f +- %.4f m, "
          "expected %.4f m; mean ratio - 1 = %+.4f %%; per-event |ratio - 1|: median %.3f %%, max %.3f %%" %
          (r_traj.mean(), r_traj.std(ddof=1) / math.sqrt(len(r_traj)), r_hits.mean(),
           r_hits.std(ddof=1) / math.sqrt(len(r_hits)), r_expected, 100 * (r_traj.mean() / r_hits.mean() - 1),
           100 * np.median(per_event), 100 * per_event.max()))
    expect(rel < 0.01, "circle-fit radius of the exported trajectories is within 1 %% of the one from hits.arrow "
           "(mean difference %.4f %%)" % (100 * rel))
    expect(abs(r_traj.mean() / r_expected - 1) < 0.01, "and within 1 %% of the radius expected from pT = 0.2998 B R "
           "(%.3f %%)" % (100 * (r_traj.mean() / r_expected - 1)))

    # ================================================================== the calorimeter
    cal_spec = examples / "electron_shower.json"
    cal = {}
    for flag in ("off", "on"):
        for threads in (1, 4):
            cal[(flag, threads)] = simulate(cal_spec, ["--export-3d"] if flag == "on" else [], threads)
    coids = {k: json.loads(read(v, "run_summary.json"))["oid"] for k, v in cal.items()}
    print("electron_shower.json (20 events, seed 42): OID of hits.arrow")
    for k in sorted(coids):
        print("   export %-3s %d thread(s): %s" % (k[0], k[1], coids[k]))
    expect(len({read(v, "hits.arrow") for v in cal.values()}) == 1 and len(set(coids.values())) == 1,
           "electron_shower: hits.arrow is byte-identical with the export on and off, with 1 and 4 threads (OID %s)" %
           coids[("off", 1)])
    cprov = json.loads(read(cal[("off", 1)], "provenance.json"))
    want = recorded.get((platform_key(cprov), "electron_shower"))
    if want is not None:
        expect(coids[("off", 1)] == want, "electron_shower: OID equals the value recorded for this build (%s)" % want)
    expect(all(read(cal[("on", 1)], n) == read(cal[("on", 4)], n) for n in ("geometry.glb", "trajectories.json",
                                                                          "provenance.json")),
           "electron_shower: both exported files and provenance.json identical for 1 and 4 threads")
    cprov_on = json.loads(read(cal[("on", 1)], "provenance.json"))
    check_glb("calorimeter glb", read(cal[("on", 1)], "geometry.glb"), cprov_on, 20, "layered_calorimeter")
    cdoc, cbin = parse_glb(read(cal[("on", 1)], "geometry.glb"))
    g = cprov_on["spec"]["geometry"]
    layer_mm = g["absorber"]["thickness_mm"] + g["active"]["thickness_mm"]
    front = -0.5 * g["layers"] * layer_mm
    centres_ok = all(
        abs(n["translation"][2] * 1000.0 - (front + i // 2 * layer_mm +
                                             (0.5 * g["absorber"]["thickness_mm"] if i % 2 == 0 else
                                              g["absorber"]["thickness_mm"] + 0.5 * g["active"]["thickness_mm"]))) < 1e-3
        and n["name"] == ("absorber" if i % 2 == 0 else "active") and n["extras"]["copy_no"] == i // 2 and
        n["extras"]["sensitive"] is True for i, n in enumerate(cdoc["nodes"]))
    expect(centres_ok, "calorimeter glb: nodes alternate absorber/active per layer, centres in z are right (mm), "
           "copy_no = layer, both volumes sensitive as in the spec")
    ctraj = json.loads(read(cal[("on", 1)], "trajectories.json"))
    expect(not list(Draft202012Validator(schema).iter_errors(ctraj)), "electron_shower trajectories.json validates")
    csz = {n: len(read(cal[("on", 1)], n)) for n in ("geometry.glb", "trajectories.json")}
    print("   electron_shower, 10 events exported, default limits: geometry.glb %d bytes, trajectories.json %d bytes" %
          (csz["geometry.glb"], csz["trajectories.json"]))
    for e in ctraj["events"]:
        print("      event %d: %d tracks created, %d kept, %d dropped" %
              (e["event_id"], e["tracks_total"], len(e["tracks"]), e["dropped_tracks"]))
    expect(all(len(e["tracks"]) <= 500 and e["dropped_tracks"] == e["tracks_total"] - len(e["tracks"]) for e in ctraj["events"]) and
           all(len(e["tracks"]) == 500 for e in ctraj["events"]), "the 500-track cap applies to every event of the shower")
    low = min(t["kinetic_energy_start_GeV"] for e in ctraj["events"] for t in e["tracks"] if t["parent_id"] != 0)
    expect(low >= 0.001, "no kept secondary is below the 1 MeV default (lowest %.6f GeV)" % low)

    # ================================================================== command line and failure
    expect(cli(muon_spec, ["--export-events", "5"])[0].returncode == 2, "--export-events without --export-3d is a usage error (exit 2)")
    expect(cli(muon_spec, ["--export-min-energy", "0.1"])[0].returncode == 2, "--export-min-energy without --export-3d is a usage error")
    for bad in (["--export-events", "0"], ["--export-events", "101"], ["--export-min-energy", "-1"],
                ["--export-min-energy", "10001"]):
        p, o = cli(muon_spec, ["--export-3d"] + bad)
        expect(p.returncode == 2 and not os.path.exists(o) and p.stdout == "", "%s is a usage error, no folder" % " ".join(bad))
    few = simulate(muon_spec, ["--export-3d", "--export-events", "3"], 1)
    expect(len(json.loads(read(few, "trajectories.json"))["events"]) == 3, "--export-events 3 exports 3 events")
    many = simulate(muon_spec, ["--export-3d", "--export-events", "100"], 1)
    jm = json.loads(read(many, "trajectories.json"))
    expect(len(jm["events"]) == 50 and jm["events_requested"] == 100,
           "--export-events 100 on a 50-event run exports 50 and records events_requested 100")

    proc, out = cli(muon_spec, ["--export-3d"], 2, env={"VIKSHEP_TEST_EXPORT_FAIL": "1"}, progress="jsonl")
    err = [json.loads(line) for line in proc.stdout.splitlines()][-1]
    leftovers = [n for n in os.listdir(work) if n.startswith(".run%d" % counter[0]) or n == os.path.basename(out)]
    expect(proc.returncode == 1 and err["event"] == "error" and err["code"] == "INTERNAL" and "3-D export failed" in err["message"]
           and not leftovers, "an export failure is INTERNAL with a clear message, exit 1, no folder and no partial folder (%s)" %
           err["message"])
finally:
    shutil.rmtree(work, ignore_errors=True)

if problems:
    print("\n%d check(s) failed" % len(problems))
    sys.exit(1)
print("\nall export checks passed")
