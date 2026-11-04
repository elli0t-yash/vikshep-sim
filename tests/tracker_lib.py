"""Helpers of the tracker tests: build a tracker spec, run vikshep-sim, read hits.arrow, fit a
straight line and a circle. Needs numpy and pyarrow (both are in environment.yml)."""
import json
import math
import os
import subprocess

import numpy as np
import pyarrow.ipc as ipc

C_GEV_PER_T_M = 0.299792458  # pT[GeV] = C * B[T] * R[m]
MUON_MASS_MEV = 105.6583755  # PDG 2024; Geant4 uses the same value to well below the tolerances used

# Eight silicon layers, 0.3 mm walls, radii 50 to 400 mm in steps of 50 mm, 1.2 m long.
LAYER_RADII_MM = [50, 100, 150, 200, 250, 300, 350, 400]
LAYER_THICKNESS_MM = 0.3
LAYER_HALF_LENGTH_MM = 600


def tracker_spec(particle="mu-", energy_gev=1.0, field_t=(0, 0, 0), direction=None, position=None,
                 events=200, seed=42, version="1.1", radii=None):
    gun = {"particle": particle, "energy_GeV": energy_gev}
    if direction is not None:
        gun["direction"] = list(direction)
    if position is not None:
        gun["position_mm"] = list(position)
    spec = {
        "spec_version": version,
        "template": "tracker_barrel",
        "geometry": {"layers": [{"radius_mm": r, "thickness_mm": LAYER_THICKNESS_MM,
                                 "half_length_mm": LAYER_HALF_LENGTH_MM}
                                for r in (radii or LAYER_RADII_MM)]},
        "gun": gun,
        "events": events,
        "seed": seed,
    }
    if field_t is not None:
        spec["field"] = {"B_T": list(field_t)}
    return spec


def run(exe, spec, out_dir, threads=1):
    """Runs `vikshep-sim run`; returns the parsed run_summary.json and provenance.json."""
    spec_path = out_dir + ".spec.json"
    with open(spec_path, "w") as f:
        json.dump(spec, f)
    proc = subprocess.run([exe, "run", "--spec", spec_path, "--out", out_dir, "--threads", str(threads),
                           "--progress", "none"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError("vikshep-sim run failed with exit code %d\nstdout: %s\nstderr tail: %s" %
                           (proc.returncode, proc.stdout, proc.stderr[-800:]))
    with open(os.path.join(out_dir, "run_summary.json")) as f:
        summary = json.load(f)
    with open(os.path.join(out_dir, "provenance.json")) as f:
        provenance = json.load(f)
    return summary, provenance


def read_hits(out_dir):
    """hits.arrow as a dict of numpy arrays (string columns as lists)."""
    table = ipc.open_file(os.path.join(out_dir, "hits.arrow")).read_all()
    out = {}
    for name in table.column_names:
        col = table.column(name)
        out[name] = col.to_pylist() if name == "volume" else col.to_numpy()
    return out


def primary_tracks(hits, layers):
    """Per event, the primary particle's hits, one per layer: {event_id: array (layers, 3) of x, y, z}.
    Events where the primary does not give exactly one hit in every layer are returned in `skipped`
    (event_id -> list of layers with hits)."""
    tracks, skipped = {}, {}
    ev, tid, layer = hits["event_id"], hits["track_id"], hits["layer"]
    for e in sorted(set(ev.tolist())):
        sel = (ev == e) & (tid == 1)
        found = layer[sel].tolist()
        if sorted(found) != list(range(layers)):
            skipped[e] = found
            continue
        order = np.argsort(layer[sel])
        xyz = np.stack([hits["x"][sel], hits["y"][sel], hits["z"][sel]], axis=1)[order]
        tracks[e] = xyz
    return tracks, skipped


def expected_momentum_mev(kinetic_gev, mass_mev=MUON_MASS_MEV):
    t = kinetic_gev * 1000.0
    return math.sqrt(t * t + 2.0 * t * mass_mev)


def fit_line(points):
    """Total least squares line through 3D points. Returns (point on line, unit direction,
    perpendicular residuals in the units of the points)."""
    centre = points.mean(axis=0)
    _, _, vt = np.linalg.svd(points - centre)
    direction = vt[0]
    d = points - centre
    perp = d - np.outer(d @ direction, direction)
    return centre, direction, np.linalg.norm(perp, axis=1)


def fit_circle(x, y):
    """Least squares circle through points (geometric residuals). A Kasa fit starts a few
    Gauss-Newton steps on the distances. Returns (cx, cy, r, rms residual)."""
    a = np.stack([2 * x, 2 * y, np.ones_like(x)], axis=1)
    b = x * x + y * y
    (cx, cy, c), *_ = np.linalg.lstsq(a, b, rcond=None)
    r = math.sqrt(max(c + cx * cx + cy * cy, 1e-12))
    for _ in range(30):
        dx, dy = x - cx, y - cy
        dist = np.hypot(dx, dy)
        resid = dist - r
        jac = np.stack([-dx / dist, -dy / dist, -np.ones_like(dist)], axis=1)
        step, *_ = np.linalg.lstsq(jac, -resid, rcond=None)
        cx, cy, r = cx + step[0], cy + step[1], r + step[2]
        if np.max(np.abs(step)) < 1e-12:
            break
    resid = np.hypot(x - cx, y - cy) - r
    return cx, cy, r, float(np.sqrt(np.mean(resid ** 2)))


def highland_theta0(p_mev, x_over_x0, mass_mev=MUON_MASS_MEV):
    """Highland/PDG width of the projected multiple scattering angle (rad) for a particle of
    momentum p and charge 1 crossing x/X0 radiation lengths: 13.6 MeV / (beta c p) * sqrt(x/X0) *
    (1 + 0.038 ln(x/X0))."""
    beta = p_mev / math.hypot(p_mev, mass_mev)
    return 13.6 / (beta * p_mev) * math.sqrt(x_over_x0) * (1 + 0.038 * math.log(x_over_x0))
