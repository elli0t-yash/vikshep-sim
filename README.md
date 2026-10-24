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
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
  -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build

# 3. run
./build/vikshep-sim info --json
```

The two configure flags make CMake prefer the environment's libraries over
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
- `vikshep-sim dev-run --spec <file> [--threads N]` is EXPERIMENTAL and outside
  the sidecar contract. It runs the spec in Geant4 and prints, on stdout, the
  mean energy deposited per layer in the absorber and in the active slab, the
  mean total and the mean fraction of the incident energy. Geant4's own output
  goes to stderr. It will be replaced by `run`.
- `vikshep-sim run` and `vikshep-sim datasets` are stubs. They print
  "not implemented" to stderr and exit with code 2.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

## Documents

- docs/SPEC.md and docs/spec.schema.json: the simulation spec.
- docs/HITS.md: the hits.arrow format.
- docs/SIDECAR.md: the command-line contract with the desktop app.

## Rules check

```sh
scripts/check-rules.sh
```

## Licence

AGPL-3.0, see LICENSE. Geant4 credit is in NOTICE.
