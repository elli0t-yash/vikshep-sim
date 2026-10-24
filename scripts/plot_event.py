#!/usr/bin/env python3
"""Plot one event of a vikshep-sim run folder.

    python scripts/plot_event.py <run_dir> --event 0 --out shower.png
    python scripts/plot_event.py <run_dir_1> <run_dir_2> --event 0 --out compare.png

For each run folder (hits.arrow, provenance.json, run_summary.json):

  left panel   the hits of the chosen event in the z-y plane (mm), drawn over an outline
               of the layers; marker area is proportional to the energy deposited (MeV)
  right panel  energy deposited per layer (MeV): the chosen event, and the mean over all
               events of the run

With several run folders the figure has one column per run (hits on top, layers below),
so that runs can be compared side by side.

Only hits in the sensitive volumes are in hits.arrow, so "energy" is the energy deposited
in those volumes. The geometry is read from the canonical spec in provenance.json.
Needs pyarrow and matplotlib (both are in environment.yml).
"""
import argparse
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # write files, never open a window
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pyarrow.ipc as ipc  # noqa: E402
from matplotlib.patches import Rectangle  # noqa: E402

# Colours of the two slab types; hits in a slab are drawn in its colour.
ABSORBER_COLOUR = "#c9ccd6"
ACTIVE_COLOUR = "#bfe0f0"
HIT_COLOURS = {"absorber": "#7a1f1f", "active": "#0b4f8a"}

# Marker area (points squared) of the largest hit of the event, and of the smallest.
MAX_MARKER_AREA = 60.0
MIN_MARKER_AREA = 2.0


class RunData:
    """What the plots need from one run folder."""

    def __init__(self, run_dir, event):
        self.dir = Path(run_dir)
        for name in ("hits.arrow", "provenance.json", "run_summary.json"):
            if not (self.dir / name).is_file():
                raise SystemExit("%s is not a run folder: %s is missing" % (self.dir, name))
        self.provenance = json.loads((self.dir / "provenance.json").read_text())
        self.summary = json.loads((self.dir / "run_summary.json").read_text())
        spec = self.provenance["spec"]
        self.spec = spec
        self.geometry = spec["geometry"]
        self.layers = int(self.geometry["layers"])
        self.events = int(self.summary["events_requested"])
        self.event = event
        if not 0 <= event < self.events:
            raise SystemExit("%s: event %d does not exist (the run has events 0 to %d)" %
                             (self.dir, event, self.events - 1))
        self._read_hits()

    def _read_hits(self):
        """Reads the file batch by batch: keeps the rows of the chosen event and sums the
        energy per layer, so a large file is not held in memory."""
        reader = ipc.open_file(self.dir / "hits.arrow")
        self.volume_names = None
        energy_by_layer = np.zeros(self.layers)
        rows = {"z": [], "y": [], "edep": [], "volume": [], "layer": []}
        for i in range(reader.num_record_batches):
            batch = reader.get_batch(i)
            column = batch.column("volume")
            if self.volume_names is None:
                self.volume_names = column.dictionary.to_pylist()
            layer = batch.column("layer").to_numpy(zero_copy_only=False)
            edep = batch.column("edep").to_numpy(zero_copy_only=False)
            energy_by_layer += np.bincount(layer, weights=edep, minlength=self.layers)[:self.layers]
            event_id = batch.column("event_id").to_numpy(zero_copy_only=False)
            mine = event_id == self.event
            if mine.any():
                rows["z"].append(batch.column("z").to_numpy(zero_copy_only=False)[mine])
                rows["y"].append(batch.column("y").to_numpy(zero_copy_only=False)[mine])
                rows["edep"].append(edep[mine])
                rows["volume"].append(column.indices.to_numpy(zero_copy_only=False)[mine])
                rows["layer"].append(layer[mine])
        if self.volume_names is None:  # a run without any hit has no record batch
            self.volume_names = list(self.spec["sensitive"])
        cat = {k: (np.concatenate(v) if v else np.array([])) for k, v in rows.items()}
        self.z, self.y, self.edep, self.volume = cat["z"], cat["y"], cat["edep"], cat["volume"]
        self.layer = cat["layer"].astype(int)
        self.mean_energy_by_layer = energy_by_layer / self.events

        # the chosen event's energy per layer
        self.event_energy_by_layer = np.zeros(self.layers)
        if len(self.edep):
            self.event_energy_by_layer = np.bincount(self.layer, weights=self.edep,
                                                     minlength=self.layers)[:self.layers]

    def label(self):
        gun = self.spec["gun"]
        return "%s %g GeV" % (gun["particle"], gun["energy_GeV"])


def draw_outline(ax, run):
    """The layers in the z-y plane: absorber and active slabs as rectangles."""
    g = run.geometry
    a, t = g["absorber"]["thickness_mm"], g["active"]["thickness_mm"]
    half = 0.5 * g["transverse_mm"]
    front = -0.5 * run.layers * (a + t)
    for i in range(run.layers):
        z0 = front + i * (a + t)
        ax.add_patch(Rectangle((z0, -half), a, 2 * half, facecolor=ABSORBER_COLOUR,
                               edgecolor="#888a94", linewidth=0.4, zorder=1))
        ax.add_patch(Rectangle((z0 + a, -half), t, 2 * half, facecolor=ACTIVE_COLOUR,
                               edgecolor="#6a99ad", linewidth=0.4, zorder=1))
    ax.set_xlim(front - 0.05 * run.layers * (a + t), -front + 0.05 * run.layers * (a + t))
    ax.set_ylim(-half * 1.2, half * 1.55)  # room above the stack for the legend


def draw_hits(ax, run):
    draw_outline(ax, run)
    n = len(run.edep)
    if n:
        # marker area proportional to edep, between MIN_ and MAX_MARKER_AREA
        top = run.edep.max()
        area = np.maximum(MIN_MARKER_AREA, MAX_MARKER_AREA * run.edep / top)
        for k, name in enumerate(run.volume_names):
            sel = run.volume == k
            if sel.any():
                ax.scatter(run.z[sel], run.y[sel], s=area[sel], color=HIT_COLOURS.get(name, "k"),
                           alpha=0.7, linewidths=0, zorder=3,
                           label="%s hits (%d)" % (name, int(sel.sum())))
        ax.legend(loc="upper left", fontsize=7, framealpha=0.9, title="marker area ~ edep (max %.3g MeV)" % top,
                  title_fontsize=7)
    else:
        ax.text(0.5, 0.5, "no hits in event %d" % run.event, transform=ax.transAxes, ha="center",
                va="center", fontsize=10)
    ax.set_xlabel("z (mm)")
    ax.set_ylabel("y (mm)")
    ax.set_title("%s, event %d: %d hits, %.4g MeV deposited in %s\nhits in the z-y plane (mm) over the "
                 "layers (grey: absorber, blue: active)" %
                 (run.label(), run.event, n, run.edep.sum(), "+".join(run.volume_names)), fontsize=8)
    ax.set_aspect("auto")


def draw_layers(ax, run):
    x = np.arange(run.layers)
    ax.bar(x, run.event_energy_by_layer, width=0.7, color="#3b7bbf", alpha=0.85,
           label="event %d" % run.event)
    ax.plot(x, run.mean_energy_by_layer, color="#d1491f", marker="o", markersize=4, linewidth=1.2,
            label="mean over all %d events" % run.events)
    ax.set_xlabel("layer (0 = front)")
    ax.set_ylabel("energy deposited in %s (MeV)" % "+".join(run.volume_names))
    ax.set_xticks(x)
    ax.set_title("%s: energy by layer (MeV)\nevent %d and the mean over all events of the run" %
                 (run.label(), run.event), fontsize=8)
    ax.legend(fontsize=7)
    ax.grid(axis="y", linewidth=0.3)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run_dir", nargs="+", help="run folder(s) written by 'vikshep-sim dev-run --out'")
    parser.add_argument("--event", type=int, default=0, help="event id to draw (default 0)")
    parser.add_argument("--out", required=True, help="PNG file to write")
    parser.add_argument("--dpi", type=int, default=130)
    args = parser.parse_args()

    runs = [RunData(d, args.event) for d in args.run_dir]
    if len(runs) == 1:
        fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
        draw_hits(axes[0], runs[0])
        draw_layers(axes[1], runs[0])
    else:
        fig, axes = plt.subplots(2, len(runs), figsize=(6.4 * len(runs), 8.6), squeeze=False)
        for j, run in enumerate(runs):
            draw_hits(axes[0][j], run)
            draw_layers(axes[1][j], run)
        # same vertical scale in the lower row, so the runs can be compared by eye
        top = max(ax.get_ylim()[1] for ax in axes[1])
        for ax in axes[1]:
            ax.set_ylim(0, top)
    fig.tight_layout()
    out = Path(args.out)
    fig.savefig(out, dpi=args.dpi)
    plt.close(fig)
    print("wrote %s (%d bytes)" % (out, out.stat().st_size))


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyError, ValueError) as error:
        sys.exit("plot_event.py: %s" % error)
