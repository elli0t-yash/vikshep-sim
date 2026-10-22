# vikshep-sim

The Simulate pillar of Vikshep Studio. It embeds and drives Geant4 to turn a
JSON spec into hits.arrow, provenance.json and run_summary.json. Geant4 does the
transport; this repository only drives it and writes files.

Geant4 is developed by the Geant4 Collaboration and is not part of Vikshep.

Status: milestone Y1 (skeleton, CLI and CI). Simulation is not implemented yet.
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

## Commands

- `vikshep-sim info [--json]` prints tool and Geant4 build information.
- `vikshep-sim run` and `vikshep-sim datasets` are stubs. They print
  "not implemented" to stderr and exit with code 2.

## Rules check

```sh
scripts/check-rules.sh
```

## Licence

AGPL-3.0, see LICENSE. Geant4 credit is in NOTICE.
