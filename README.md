# vikshep-sim

The Simulate pillar of Vikshep Studio. It embeds and drives Geant4 to turn a
JSON spec into hits.arrow, provenance.json and run_summary.json. Geant4 does the
transport; this repository only drives it and writes files.

Geant4 is developed by the Geant4 Collaboration and is not part of Vikshep.

Status: spec 1.0 and 1.1 (the layered calorimeter, and since 1.1 a tracker barrel with an
optional uniform magnetic field), the validator, `run` and `dev-run` exist. See STATUS.md.

Determinism target: identical output on the same build, platform and seed.
Results are not promised to be identical across platforms.

## Quick start

Requires micromamba (or mamba/conda).

```sh
# 1. create the environment from the lock file of your platform (exact packages, see "Lock files")
micromamba create -n vikshep-sim -f conda-osx-arm64.lock      # macOS, Apple silicon
micromamba create -n vikshep-sim -f conda-linux-64.lock       # Linux, x86-64
micromamba activate vikshep-sim
# any other platform: solve environment.yml instead (the versions in it are exact, builds may differ)
#   micromamba create -n vikshep-sim -f environment.yml

# 2. build
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
  -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build

# 3. run
./build/vikshep-sim info --json
```

`Release` is the default build type if none is given; it is recorded in `provenance.json`
(`build_type`) and the build refuses `-ffast-math`. The prefix flags make CMake prefer the environment's libraries over
anything in the macOS SDK or the system.

## Lock files

`environment.yml` names the exact version of every package that affects the build or the tests
(Geant4 11.4.3, CMake, Ninja, the C++ compiler metapackage, CLI11, nlohmann_json, Python, pyarrow,
numpy, matplotlib, and so on). `conda-linux-64.lock` and `conda-osx-arm64.lock` go one step
further: they list every package of the environment with its build and hash (an explicit conda
spec, so nothing is solved at install time), and **CI installs from them**. The compilers (clang on
macOS, gcc on Linux) come in through `cxx-compiler` and are fixed by the lock files, not named in
`environment.yml`.

To refresh them (after changing `environment.yml`, or to pick up newer builds):

```sh
scripts/refresh-locks.sh              # both platforms; or: scripts/refresh-locks.sh linux-64
git diff --stat conda-*.lock          # read the changes; a Geant4 or compiler change needs a re-check
```

The script needs only micromamba and python3. It solves `environment.yml` for each platform (from
any host; it sets the glibc and macOS version the target is assumed to have), and writes the URLs
and md5 hashes in install order. Running it twice gives identical files. After a refresh, build
and run the whole test suite on each platform (CI does that on the pull request) and record the
new versions in STATUS.md. A change of Geant4 or of the compiler changes simulation output; the
golden OIDs of `tests/golden/RUN_OIDS.txt` are keyed by the build and need re-recording
(`tests/check_spec10_golden.py ... --record`).

## Conda package

`recipe/recipe.yaml` builds vikshep-sim as a conda package with
[rattler-build](https://rattler.build), with Geant4 11.4.3 (noqt) pinned at build and run
time; `.github/workflows/conda.yml` builds and tests it on linux-64, osx-arm64 and osx-64
for pull requests that touch the recipe, the build or the sources. The package is not
published yet. Once a tag is uploaded to an anaconda.org channel, installing it pulls Geant4
and its data sets from conda-forge, and activating the environment sets the data set
variables:

```sh
micromamba create -n vikshep-sim -c <channel> -c conda-forge vikshep-sim
micromamba activate vikshep-sim
vikshep-sim info --json
```

Local build (needs rattler-build):

```sh
rattler-build build --recipe recipe/recipe.yaml --channel conda-forge --output-dir output
```

## Geant4 data sets

Geant4 reads its physics data (cross sections, EM tables, nuclear levels) from
directories named by environment variables such as `G4LEDATA`. Nothing is
downloaded by this project and nothing is stored in this repository.

With the conda environment of this repository the data sets are already there:
the conda-forge `geant4` package depends on one package per data set
(`geant4-data-emlow`, `geant4-data-particlexs`, ...), so
`micromamba create -f environment.yml` installs them under
`$CONDA_PREFIX/share/Geant4/data`, and **activating the environment sets the
variables**. Steps:

```sh
micromamba create -n vikshep-sim -f environment.yml   # installs the data sets too
micromamba activate vikshep-sim                        # sets G4LEDATA and the others
env | grep '^G4'                                       # check: 12 variables
```

`micromamba run -n vikshep-sim <command>` activates the environment for that
command as well. If the variables are missing (for example, the binary was
started outside the environment), commands that simulate stop before running,
print what is missing and how to install it, and exit with code 3
(`DATASETS_MISSING`). To add a missing data set to an existing environment:

```sh
micromamba install -n vikshep-sim -c conda-forge geant4-data-emlow   # example
```

`info`, `validate` and the tests that do not simulate work without data sets.

## Commands

- `vikshep-sim info [--json]` prints tool and Geant4 build information.
  `em_options` lists the EM option suffixes a physics list name can take.
  Omitting the EM option selects the physics list's default.
- `vikshep-sim validate --spec <file> [--canonical]` checks a simulation spec
  without running anything (docs/SPEC.md). Valid: exit 0. Invalid: exit 2 and
  one JSON error line on stdout.
- `vikshep-sim run --spec <file> --out <dir> [--threads N] [--progress jsonl|none]` is
  the command of the sidecar contract (docs/SIDECAR.md): it validates the spec, simulates,
  and writes `hits.arrow`, `provenance.json` and `run_summary.json` into the new folder
  `<dir>`, atomically. `--threads` defaults to 1 and never changes the output. With
  `--progress jsonl`, stdout carries only JSON lines (progress, then `done` with the OID, or
  one `error` line); everything else, including Geant4's output, goes to stderr. Exit codes:
  0 ok, 1 error, 2 bad usage or invalid spec or `--out` exists, 3 Geant4 data sets missing,
  4 cancelled (SIGTERM or SIGINT). `docs/examples/consume_run.py` shows how a program uses it:
  `python docs/examples/consume_run.py --spec docs/examples/calorimeter.json --out my_run`.
- `vikshep-sim dev-run --spec <file> [--threads N]` is EXPERIMENTAL and outside
  the sidecar contract. It runs the spec in Geant4 and prints, on stdout, the
  mean energy deposited per layer in the absorber and in the active slab, the
  mean total and the mean fraction of the incident energy. Geant4's own output
  goes to stderr. It reports the `layered_calorimeter` template only (use `run` for `tracker_barrel`). It will be replaced by `run`. With `--out <folder>` it also writes
  `hits.arrow`, `provenance.json` and `run_summary.json` (docs/HITS.md, docs/SIDECAR.md,
  section 5) into a new folder, atomically, and prints the OID; the folder must not
  exist. `--event-digests <file>` writes one fingerprint line per event.
- `vikshep-sim datasets` is a stub. It prints "not implemented" to stderr and exits with code 2.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

## Documents

- docs/SPEC.md and docs/spec.schema.json: the simulation spec (versions 1.0 and 1.1).
- docs/HITS.md: the hits.arrow format.
- docs/SIDECAR.md: the command-line contract with the desktop app.
- docs/EXPORT3D.md: the optional 3-D export (`geometry.glb`, `trajectories.json`).

## Tracker barrel and magnetic field (spec 1.1)

`docs/examples/tracker_muon.json` is a barrel of six 0.3 mm silicon layers (radii 50 to 300 mm,
1 m long) in a uniform 2 T field along z, with a 1 GeV mu- fired from the origin along +x. Everything
about the geometry, the layer numbering, the field and its sign convention is in docs/SPEC.md,
sections 2.5 to 2.7. A spec written as 1.0 keeps its meaning: the same bytes, OID and `spec_oid`.

```sh
./build/vikshep-sim run --spec docs/examples/tracker_muon.json --out tracker_run
python scripts/plot_event.py tracker_run --event 0 --out tracker.png
```

## 3-D export (optional)

`run --export-3d` also writes `geometry.glb` (glTF 2.0, one node per volume) and `trajectories.json`
(the tracks of the first events), for looking at a run. They are a subset of the simulation and not
part of the OID: `hits.arrow` and its OID are the same with and without the flag. Formats, units and
exactly what is kept: docs/EXPORT3D.md. `--export-events N` (default 10, at most 100) and
`--export-min-energy GeV` (default 0.001) tune it; there is also a cap of 500 tracks per event, and
each event says how many tracks were dropped.

```sh
./build/vikshep-sim run --spec docs/examples/tracker_muon.json --out tracker_run --export-3d
# geometry.glb is about 97 kB for this tracker (too large to keep in docs/examples), so the example
# outputs are made with the command above instead of being checked in.
python3 -m http.server 8000          # in a folder that contains tracker_run and a copy of, or a path to, scripts/
# then open  http://localhost:8000/scripts/view_3d.html?run=/tracker_run
```

`scripts/view_3d.html` is a local page (three.js 0.160.0 from the jsDelivr CDN, with integrity hashes) with orbit controls,
an event selector, thick tracks drawn over translucent volumes (opacity and track-width sliders), the
primary highlighted, charged-only / primary-only / vertex-point toggles, dimmed neutral tracks and
per-colour track counts; run `python3 -m http.server` from the directory that holds both
the repository's `scripts/` folder and the run folder (the repository root works if the run folder is
inside it), or pick the two files with the page's file choosers. It is only for looking; it is not the
Studio viewer.

## Plotting a run

`scripts/plot_event.py` draws one event of a run folder (needs pyarrow and
matplotlib, which are in `environment.yml`): the hits in the z-y plane over an outline
of the layers (marker area proportional to the energy deposited), and the energy
deposited per layer for that event and the mean over all events. Axes and titles
state their units (mm, MeV). The geometry is read from `provenance.json`.

```sh
./build/vikshep-sim dev-run --spec docs/examples/electron_shower.json --out electron_run
python scripts/plot_event.py electron_run --event 0 --out shower.png
```

For a tracker run the picture has the transverse view (x-y, layers as rings), the side view (z-r)
and the energy per layer; with a field it also draws a circle fitted to the primary track and
prints its radius and the pT it implies (`pT = 0.299792458 B R`).

Several run folders give one column per run, for comparing runs side by side:
`python scripts/plot_event.py electron_run pion_run --event 0 --out compare.png`.

Only hits in the sensitive volumes are in `hits.arrow`, so "energy" means the energy
deposited in those volumes (the two example specs below record both slab types).

### Electron and pion

`docs/examples/electron_shower.json` and `docs/examples/pion_shower.json` are the same
experiment (10 layers of 10 mm Pb and 5 mm liquid argon, 5 GeV, 20 events, seed 42,
both slab types sensitive) with an electron and a negative pion as the beam particle.

```sh
scripts/compare_particles.sh compare_out      # runs both, writes electron.png, pion.png, compare.png
```

The script refuses to write into a folder that already exists. It expects the program
at `build/vikshep-sim` of the repository the script is in, whatever the current directory (set `VIKSHEP_SIM_BIN` or pass `--exe` for another path) and the environment to be
active.

## Rules check

```sh
scripts/check-rules.sh
```

## Licence

AGPL-3.0, see LICENSE. Geant4 credit is in NOTICE.
