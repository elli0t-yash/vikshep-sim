#!/usr/bin/env python3
"""Physics checks on format 2 of trajectories.json (docs/EXPORT3D.md section 7), with the numbers printed.

Usage: check_export_physics.py <vikshep-sim> <examples dir>
Exit code 77 (ctest: skipped) when the Geant4 data sets are missing.

The checks, and the tolerance of each (the measured values are printed next to the tolerance):
  1. t_ns never decreases along a track.
  2. a photon's time difference between two points is the length of the segment divided by c.
  3. on the 100 MeV gamma example (nothing dropped), per event: the sum of edep over all points plus the
     kinetic energy of every track that left the world equals the primary energy; and the sum of edep
     equals the energy in hits.arrow (both volumes are sensitive).
  4. the process names of the gamma example include the expected ones (Compton, photoelectric, pair
     production), and every secondary is listed by an interaction of its parent.
  5. on the muon-in-a-field example the exported circle radius agrees with the fit to the hits and
     with pT = 0.2998 B R; delta rays are listed as interactions.
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tracker_lib as tl  # noqa: E402
import vikshep_testlib  # noqa: E402

exe, examples = sys.argv[1], Path(sys.argv[2])
vikshep_testlib.require_data_sets()

C_MM_PER_NS = 299.792458
TOL_TIME_NS = 2e-5          # per segment: 1 fs rounding of two times plus 1 um rounding of each coordinate
TOL_BALANCE_GEV = 5e-6      # 5 keV per event: 0.1 keV rounding per number, summed over the steps of the event
TOL_HITS_GEV = 5e-6         # 5 keV per event between the exported deposits and hits.arrow
TOL_RADIUS = 0.01           # 1 %, the tolerance of the earlier circle-fit test (check_export3d.py)
TOL_RADIUS_EXPECTED = 0.02  # 2 % against pT = 0.2998 B R: six layers give a coarse fit

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


work = tempfile.mkdtemp(prefix="vikshep-physics-")


def run(spec_path, name, threads=2, extra=("--export-min-energy", "0")):
    out = os.path.join(work, name)
    proc = subprocess.run([exe, "run", "--spec", str(spec_path), "--out", out, "--threads", str(threads),
                           "--progress", "none", "--export-3d", *extra], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError("run failed: " + proc.stderr[-500:])
    return out, json.loads(Path(out, "trajectories.json").read_text())


def segment_deviation(track):
    """max over the segments of |dt - length / c| in ns, for a track moving at c."""
    p = np.array([q[:3] for q in track["points"]])
    t = np.array([q[3] for q in track["points"]])
    if len(p) < 2:
        return 0.0
    length = np.linalg.norm(np.diff(p, axis=0), axis=1)
    return float(np.abs(np.diff(t) - length / C_MM_PER_NS).max())


try:
    # ================================================================== 100 MeV gamma into the calorimeter
    gamma_dir, gamma = run(examples / "teaching_gamma.json", "gamma")
    events = gamma["events"]
    expect(all(e["dropped_tracks"] == 0 and e["dropped_interactions"] == 0 for e in events),
           "teaching_gamma: nothing dropped in %d events (tracks per event: %s)" %
           (len(events), ", ".join(str(e["tracks_total"]) for e in events)))
    hits = tl.read_hits(gamma_dir)

    worst_time = worst_dt = 0.0
    monotonic = True
    for e in events:
        for t in e["tracks"]:
            times = [p[3] for p in t["points"]]
            monotonic = monotonic and all(b >= a for a, b in zip(times, times[1:]))
            if t["particle"] == "gamma":
                worst_dt = max(worst_dt, segment_deviation(t))
    expect(monotonic, "time never decreases along any track (all tracks of %d events)" % len(events))
    expect(worst_dt <= TOL_TIME_NS,
           "photon segments: max |dt - length/c| = %.2e ns, tolerance %.0e ns" % (worst_dt, TOL_TIME_NS))

    print("   energy balance per event (GeV): primary %.6f" % events[0]["tracks"][0]["kinetic_energy_start_GeV"])
    worst_bal = worst_hit = 0.0
    for e in events:
        primary = next(t for t in e["tracks"] if t["parent_id"] == 0)["kinetic_energy_start_GeV"]
        deposited = sum(p[5] for t in e["tracks"] for p in t["points"])
        left = sum(t["points"][-1][4] for t in e["tracks"] if t["end_process"] == "Transportation")
        sel = hits["event_id"] == e["event_id"]
        in_hits = float(hits["edep"][sel].sum()) / 1000.0     # hits.arrow is in MeV
        bal = abs(deposited + left - primary)
        dh = abs(deposited - in_hits)
        worst_bal, worst_hit = max(worst_bal, bal), max(worst_hit, dh)
        print("      event %d: deposited %.7f + left the world %.7f = %.7f; |sum - primary| = %.2e GeV; "
              "|deposited - hits.arrow| = %.2e GeV" % (e["event_id"], deposited, left, deposited + left, bal, dh))
    expect(worst_bal <= TOL_BALANCE_GEV, "deposited + left the world = primary energy: worst %.2e GeV, tolerance %.0e GeV" %
           (worst_bal, TOL_BALANCE_GEV))
    expect(worst_hit <= TOL_HITS_GEV, "exported deposits = energy in hits.arrow: worst %.2e GeV, tolerance %.0e GeV" %
           (worst_hit, TOL_HITS_GEV))

    names = Counter(i["process"] for e in events for i in e["interactions"])
    print("   interaction processes over %d events: %s" % (len(events), dict(sorted(names.items()))))
    expected = {"compt": "Compton scattering", "phot": "photoelectric absorption", "conv": "pair production"}
    expect(all(names[k] > 0 for k in expected), "the gamma example has %s" % ", ".join(expected.values()))
    expect(names["annihil"] > 0, "and positron annihilation")
    unlinked = 0
    bad_point = 0
    for e in events:
        listed = {s for i in e["interactions"] for s in i["secondaries"]}
        unlinked += sum(1 for t in e["tracks"] if t["parent_id"] > 0 and t["track_id"] not in listed)
        by_id = {t["track_id"]: t for t in e["tracks"]}
        for i in e["interactions"]:
            p = by_id[i["track_id"]]["points"][i["point_index"]]
            bad_point += not (p[:3] == i["position_mm"] and p[3] == i["t_ns"] and p[5] == i["edep_GeV"] and
                              p[6] == i["process"])
    expect(unlinked == 0, "every secondary track is listed by an interaction of its parent (%d unlisted)" % unlinked)
    expect(bad_point == 0, "every interaction repeats the position, time, deposit and process of its point (%d differ)" %
           bad_point)

    # ================================================================== muon in a field
    mu_dir, mu = run(examples / "teaching_muon_field.json", "muon")
    spec = json.loads(Path(examples / "teaching_muon_field.json").read_text())
    b = spec["field"]["B_T"][2]
    hits = tl.read_hits(mu_dir)
    muon_bal = 0.0
    for e in mu["events"]:
        primary = next(t for t in e["tracks"] if t["parent_id"] == 0)["kinetic_energy_start_GeV"]
        deposited = sum(p[5] for t in e["tracks"] for p in t["points"])
        left = sum(t["points"][-1][4] for t in e["tracks"] if t["end_process"] == "Transportation")
        muon_bal = max(muon_bal, abs(deposited + left - primary))
    expect(muon_bal <= TOL_BALANCE_GEV and all(e["dropped_tracks"] == 0 for e in mu["events"]),
           "muon example: deposited + left the world = primary energy, worst %.2e GeV, tolerance %.0e GeV" %
           (muon_bal, TOL_BALANCE_GEV))
    deltas = [i for e in mu["events"] for i in e["interactions"] if i["process"] == "muIoni" and i["secondaries"]]
    print("   muon example: %d delta-ray interactions (muIoni with a secondary) in %d events" %
          (len(deltas), len(mu["events"])))
    expect(len(deltas) > 0, "the muon example shows delta rays")
    ratios = []
    for e in mu["events"]:
        pts = next(t for t in e["tracks"] if t["parent_id"] == 0)["points"]
        xy = np.array([q[:3] for q in pts])
        r_traj = tl.fit_circle(xy[:, 0], xy[:, 1])[2]
        sel = (hits["event_id"] == e["event_id"]) & (hits["track_id"] == 1)
        r_hits = tl.fit_circle(hits["x"][sel], hits["y"][sel])[2]
        ratios.append((r_traj / 1000.0, r_hits / 1000.0))
    r_traj = np.mean([r[0] for r in ratios])
    r_hits = np.mean([r[1] for r in ratios])
    r_exp = tl.expected_momentum_mev(1.0) / 1000.0 / (tl.C_GEV_PER_T_M * b)
    print("   muon example, B = %g T: R from exported points %.4f m, from hits.arrow %.4f m, from pT = 0.2998 B R %.4f m" %
          (b, r_traj, r_hits, r_exp))
    expect(abs(r_traj / r_hits - 1) < TOL_RADIUS, "radius from exported points agrees with the fit to the hits: %+.3f %%, "
           "tolerance %.0f %%" % (100 * (r_traj / r_hits - 1), 100 * TOL_RADIUS))
    expect(abs(r_traj / r_exp - 1) < TOL_RADIUS_EXPECTED, "and with pT = 0.2998 B R: %+.3f %%, tolerance %.0f %%" %
           (100 * (r_traj / r_exp - 1), 100 * TOL_RADIUS_EXPECTED))
finally:
    shutil.rmtree(work, ignore_errors=True)

if problems:
    print("\n%d check(s) failed" % len(problems))
    sys.exit(1)
print("\nall physics checks passed")
