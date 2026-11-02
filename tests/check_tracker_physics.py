#!/usr/bin/env python3
"""Physics checks of the tracker_barrel template, with the numbers printed (docs/SPEC.md, 2.5, 2.6).

Usage: check_tracker_physics.py <vikshep-sim>
Exit code 77 (ctest: skipped) when the Geant4 data sets are missing.

  A. B = 0: a 1 GeV muon fired from the origin crosses every layer in a straight line. The hits
     of its primary track are fitted with a line; the residuals are compared with what multiple
     scattering (Highland) allows. The residual of a line is a straight-line test only to the
     accuracy of multiple scattering: it cannot be zero, Geant4 scatters the muon.
  B. B along z: the track curves in the x-y plane. A circle is fitted per event; the mean radius R
     must satisfy pT[GeV] = 0.299792458 * B[T] * R[m] (the rule of thumb is 0.3) within a stated
     tolerance, for both charges, two field strengths and a tilted direction. The expected pT comes
     from the gun (kinetic energy and the direction), not from the fit.
  C. The sign: mu- in B along +z, fired along +x, bends towards +y; mu+ the other way; reversing B
     reverses it (the convention in docs/SPEC.md, 2.6).
"""
import math
import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tracker_lib as tl  # noqa: E402
import vikshep_testlib  # noqa: E402

exe = sys.argv[1]
vikshep_testlib.require_data_sets()

# Stated tolerances.
LINE_TOLERANCE_MM = 0.5          # median over events of the largest distance of a hit from the fitted line
LINE_DIRECTION_SIGMAS = 5.0      # median angle between fitted and gun direction, in Highland widths
RADIUS_TOLERANCE = 0.005         # relative: |mean R / R expected - 1| (0.5 %)
RADIUS_SEM_SIGMAS = 4.0          # and within this many standard errors of the mean ...
RADIUS_SYSTEMATIC = 0.002        # ... plus this relative allowance (energy loss, chord midpoints)
WALL_MARGIN_MM = 0.2             # a primary hit's radius lies within the layer wall, up to this margin

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


work = tempfile.mkdtemp(prefix="vikshep-tracker-")
counter = [0]


def simulate(spec, threads=1):
    counter[0] += 1
    out = os.path.join(work, "run%d" % counter[0])
    summary, provenance = tl.run(exe, spec, out, threads)
    return tl.read_hits(out), summary


def clean_tracks(hits, layers):
    """Per event, one point per layer from the primary track (mean of its hits in that layer).
    Returns (list of (layers, 3) arrays, number of events dropped, number of layers with >1 hit)."""
    ev, tid, layer = hits["event_id"], hits["track_id"], hits["layer"]
    tracks, dropped, multi = [], 0, 0
    events = int(ev.max()) + 1 if len(ev) else 0
    for e in range(events):
        sel = (ev == e) & (tid == 1)
        pts = []
        for i in range(layers):
            s = sel & (layer == i)
            n = int(s.sum())
            if n == 0:
                pts = None
                break
            multi += n > 1
            pts.append([hits["x"][s].mean(), hits["y"][s].mean(), hits["z"][s].mean()])
        if pts is None:
            dropped += 1
        else:
            tracks.append(np.array(pts))
    return tracks, dropped, multi


try:
    layers = len(tl.LAYER_RADII_MM)
    p_mev = tl.expected_momentum_mev(1.0)
    print("1 GeV (kinetic) mu: p = %.3f MeV/c (muon mass %.4f MeV/c^2)" % (p_mev, tl.MUON_MASS_MEV))

    # ---------------------------------------------------------------- A. straight line, B = 0
    for label, direction in (("default direction (+x)", None), ("tilted [1, 0.3, 0.2]", [1, 0.3, 0.2])):
        hits, summary = simulate(tl.tracker_spec(direction=direction, field_t=(0, 0, 0)))
        tracks, dropped, multi = clean_tracks(hits, layers)
        expect(set(hits["volume"]) == {"tracker_layer"} and (hits["copy_no"] == hits["layer"]).all() and
               hits["layer"].min() == 0 and hits["layer"].max() == layers - 1,
               "A. %s: every hit is in volume tracker_layer, copy_no equals layer, layers 0 to %d" %
               (label, layers - 1))
        d = np.array(direction if direction else [1, 0, 0], dtype=float)
        d /= np.linalg.norm(d)
        sin_theta = math.hypot(d[0], d[1])
        x_over_x0 = layers * tl.LAYER_THICKNESS_MM / sin_theta / 93.7   # radiation length of Si: 93.7 mm
        theta0 = tl.highland_theta0(p_mev, x_over_x0)
        max_resid, rms_resid, angle = [], [], []
        for pts in tracks:
            _, direction_fit, resid = tl.fit_line(pts)
            if direction_fit @ d < 0:
                direction_fit = -direction_fit
            max_resid.append(resid.max())
            rms_resid.append(math.sqrt(np.mean(resid ** 2)))
            angle.append(math.acos(min(1.0, direction_fit @ d)))
        print("A. B = 0, %s: %d events, %d dropped (no hit in some layer), %d layers with more than "
              "one primary hit" % (label, len(tracks), dropped, multi))
        print("   largest distance of a hit from the fitted line: median %.4f mm, 90th percentile %.4f mm, "
              "maximum %.4f mm" % (np.median(max_resid), np.percentile(max_resid, 90), np.max(max_resid)))
        print("   rms distance %.4f mm; angle between fitted and gun direction: median %.2f mrad "
              "(Highland width of the total projected scattering angle: %.2f mrad, x/X0 = %.4f)" %
              (np.mean(rms_resid), 1e3 * np.median(angle), 1e3 * theta0, x_over_x0))
        expect(dropped == 0, "A. %s: the primary muon has a hit in every layer in every event" % label)
        expect(np.median(max_resid) < LINE_TOLERANCE_MM,
               "A. %s: median largest line residual %.4f mm < %.2f mm" %
               (label, np.median(max_resid), LINE_TOLERANCE_MM))
        expect(np.median(angle) < LINE_DIRECTION_SIGMAS * theta0,
               "A. %s: median direction error %.2f mrad < %.0f x Highland width (%.2f mrad)" %
               (label, 1e3 * np.median(angle), LINE_DIRECTION_SIGMAS, 1e3 * theta0))
        # the hits are in the walls of the layers (primary track, near-normal incidence)
        radii = np.array([np.hypot(pts[:, 0], pts[:, 1]) for pts in tracks])
        wall = np.array(tl.LAYER_RADII_MM, dtype=float)
        deviation = np.abs(radii - wall).max()
        expect(deviation < 0.5 * tl.LAYER_THICKNESS_MM + WALL_MARGIN_MM,
               "A. %s: every primary hit radius is within the layer wall (largest |r - radius_mm| = "
               "%.4f mm, half wall %.2f mm + margin %.2f mm)" %
               (label, deviation, 0.5 * tl.LAYER_THICKNESS_MM, WALL_MARGIN_MM))

    # ---------------------------------------------------------------- B. radius of curvature
    cases = [
        # label, particle, field (T), direction, expected sign of the circle centre's transverse side
        ("mu-, B = 2 T along z, along +x", "mu-", (0, 0, 2), None),
        ("mu+, B = 2 T along z, along +x", "mu+", (0, 0, 2), None),
        ("mu-, B = 1 T along z, along +x", "mu-", (0, 0, 1), None),
        ("mu-, B = 2 T along z, direction [1, 0, 0.5]", "mu-", (0, 0, 2), [1, 0, 0.5]),
    ]
    centre_side = {}
    for label, particle, field, direction in cases:
        hits, summary = simulate(tl.tracker_spec(particle=particle, field_t=field, direction=direction))
        tracks, dropped, multi = clean_tracks(hits, layers)
        d = np.array(direction if direction else [1, 0, 0], dtype=float)
        d /= np.linalg.norm(d)
        pt_mev = p_mev * math.hypot(d[0], d[1])  # momentum perpendicular to B (B along z)
        b_t = abs(field[2])
        r_expected_m = pt_mev / 1000.0 / (tl.C_GEV_PER_T_M * b_t)
        r_rule_of_thumb_m = pt_mev / 1000.0 / (0.3 * b_t)
        radii_m, centres, rms = [], [], []
        for pts in tracks:
            cx, cy, r, rms_fit = tl.fit_circle(pts[:, 0], pts[:, 1])
            radii_m.append(r / 1000.0)
            centres.append((cx, cy))
            rms.append(rms_fit)
        radii_m = np.array(radii_m)
        mean_r = radii_m.mean()
        sem = radii_m.std(ddof=1) / math.sqrt(len(radii_m))
        deviation = mean_r / r_expected_m - 1.0
        # 0.3 B R with the rounded constant, as in the rule of thumb
        pt_from_fit_gev = tl.C_GEV_PER_T_M * b_t * mean_r
        print("B. %s: %d events (%d dropped)" % (label, len(tracks), dropped))
        print("   pT expected %.4f GeV/c -> R expected %.4f m (with 0.3: %.4f m)" %
              (pt_mev / 1000.0, r_expected_m, r_rule_of_thumb_m))
        print("   R measured: mean %.4f m +- %.4f m (standard error), event-to-event spread %.4f m (%.1f %%); "
              "deviation from expected %+.3f %%" %
              (mean_r, sem, radii_m.std(ddof=1), 100 * radii_m.std(ddof=1) / mean_r, 100 * deviation))
        print("   pT from the fit: %.4f GeV/c (0.2998 B R), expected %.4f GeV/c; rms residual of the circle fit "
              "%.4f mm" % (pt_from_fit_gev, pt_mev / 1000.0, np.mean(rms)))
        expect(dropped == 0, "B. %s: the primary muon has a hit in every layer in every event" % label)
        expect(abs(deviation) < RADIUS_TOLERANCE,
               "B. %s: |R / R_expected - 1| = %.3f %% < %.1f %%" % (label, 100 * abs(deviation), 100 * RADIUS_TOLERANCE))
        allowed = RADIUS_SEM_SIGMAS * sem + RADIUS_SYSTEMATIC * r_expected_m
        expect(abs(mean_r - r_expected_m) < allowed,
               "B. %s: |R - R_expected| = %.4f m < %.0f standard errors + %.1f %% = %.4f m" %
               (label, abs(mean_r - r_expected_m), RADIUS_SEM_SIGMAS, 100 * RADIUS_SYSTEMATIC, allowed))
        centre_side[label] = float(np.median([c[1] for c in centres]))

    # ---------------------------------------------------------------- C. sign of the bend
    print("C. median y of the fitted circle centre (tracks start along +x): %s" %
          {k: round(v, 1) for k, v in centre_side.items()})
    expect(centre_side[cases[0][0]] > 0, "C. mu- in B along +z bends towards +y (centre at y > 0)")
    expect(centre_side[cases[1][0]] < 0, "C. mu+ in B along +z bends towards -y (centre at y < 0)")
    hits, _ = simulate(tl.tracker_spec(particle="mu-", field_t=(0, 0, -2)))
    tracks, _, _ = clean_tracks(hits, layers)
    flipped = float(np.median([tl.fit_circle(p[:, 0], p[:, 1])[1] for p in tracks]))
    print("C. mu- with B = -2 T along z: median centre y = %.1f mm" % flipped)
    expect(flipped < 0, "C. reversing B reverses the bend")

    # B along x with the muon along +x: no force (v parallel to B), the track stays straight
    hits, _ = simulate(tl.tracker_spec(particle="mu-", field_t=(2, 0, 0)))
    tracks, dropped, _ = clean_tracks(hits, layers)
    max_resid = [tl.fit_line(p)[2].max() for p in tracks]
    print("C. B = 2 T along x, muon along +x (v parallel to B): median largest line residual %.4f mm" %
          np.median(max_resid))
    expect(dropped == 0 and np.median(max_resid) < LINE_TOLERANCE_MM,
           "C. a field parallel to the momentum does not bend the track")
finally:
    shutil.rmtree(work, ignore_errors=True)

if problems:
    print("\n%d check(s) failed" % len(problems))
    sys.exit(1)
print("\nall tracker physics checks passed")
