# vikshep-sim

The Simulate pillar of Vikshep Studio. It embeds and drives Geant4 to turn a
JSON spec into hits.arrow, provenance.json and run_summary.json. Geant4 does the
transport; this repository only drives it and writes files.

Geant4 is developed by the Geant4 Collaboration and is not part of Vikshep.

Status: spec v1 and its validator exist; the simulation itself is not implemented yet.
See STATUS.md.

Determinism target: identical output on the same build, platform and seed.
Results are not promised to be identical across platforms.

## Quick start

Requires micromamba (or mamba/conda).

```sh
# 1. create the environment
micromamba create -n vikshep-sim -f environment.yml
micromamba activate vikshep-sim

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
  goes to stderr. It will be replaced by `run`. With `--out <folder>` it also writes
  `hits.arrow`, `provenance.json` and `run_summary.json` (docs/HITS.md, docs/SIDECAR.md,
  section 5) into a new folder, atomically, and prints the OID; the folder must not
  exist. `--event-digests <file>` writes one fingerprint line per event.
- `vikshep-sim datasets` is a stub. It prints "not implemented" to stderr and exits with code 2.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

## Documents

- docs/SPEC.md and docs/spec.schema.json: the simulation spec.
- docs/HITS.md: the hits.arrow format.
- docs/SIDECAR.md: the command-line contract with the desktop app.

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
at `build/vikshep-sim` (set `VIKSHEP_SIM` for another path) and the environment to be
active.

## Rules check

```sh
scripts/check-rules.sh
```

## Licence

AGPL-3.0, see LICENSE. Geant4 credit is in NOTICE.
