# STATUS

Last updated: 2026-10-22 (UTC, from date -u).

## Milestone Y1: skeleton, CLI and CI

- [x] CLAUDE.md with repository rules
- [x] scripts/check-rules.sh (emoji, attribution, secrets); tested on a failing file, then removed it
- [x] environment.yml, Geant4 pinned
- [x] CMakeLists.txt and sources, subcommands info, run, datasets
- [x] vikshep-sim info --json (no data sets needed, no run manager)
- [x] run and datasets stubs, exit code 2, documented in --help
- [x] .github/workflows/ci.yml (written, not yet run: nothing has been pushed)
- [x] README.md, STATUS.md, NOTICE
- [x] Local verification on macOS arm64 (see below)
- [ ] CI green on ubuntu-latest and macos-14 (needs a push, which is the owner's call)

## Versions (2026-10-22)

Geant4 11.4.3 (newest 11.x available for both linux-64 and osx-arm64 on
conda-forge at 2026-10-22; 11.4.3 is also the newest release listed there).

| Item | osx-arm64 (measured locally) | linux-64 |
|------|------------------------------|----------|
| geant4 build string | noqt_429d0ab_0 | UNCLEAR until CI. The noqt builds listed are noqt_1a35367_0 and noqt_f3881a7_0; the solver's choice is not known. |
| GDML | on (from Geant4Config.cmake) | UNCLEAR until CI |
| multithreaded | on (from Geant4Config.cmake) | UNCLEAR until CI |
| compiler | clang 21.1.8 (conda-forge) | UNCLEAR until CI |

Other packages on osx-arm64: cmake 4.4.4, ninja 1.13.2, cli11 2.7.2,
nlohmann_json 3.12.0, clhep 2.4.7.2, xerces-c 3.3.0, expat 2.8.5, zlib 1.3.2,
freetype 2.14.3.

## Decisions

- Geant4 is pinned as geant4=11.4.3=*noqt*. We use no visualization, so the
  default Qt variant (which pulls in qt6-main) is not wanted. The noqt build
  exists for both platforms and reports GDML and multithreading on (osx-arm64
  checked; linux-64 only by package listing, UNCLEAR until CI). The build-string
  glob is a deliberate exception to the "no build strings in the file" rule, as
  approved by the owner.
- Configure with -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" and
  -DCMAKE_FIND_FRAMEWORK=LAST, in README and CI, so the environment's libraries
  win over the macOS SDK.
- GDML and multithreading flags in info come from Geant4_gdml_FOUND and
  Geant4_multithreaded_FOUND in the installed Geant4Config.cmake, passed through
  configure_file.
- em_options omits the factory's empty-string entry (the default: omitting the
  EM option selects the physics list's default). physics_lists stays the full
  factory list.
- Link target is Geant4::G4physicslists (provides G4PhysListFactory).

## Extra environment dependencies (Geant4Config.cmake find_dependency)

Geant4Config.cmake calls find_dependency() for libraries the conda geant4
package only depends on at runtime. Each needs its development package:

| Package | Reason |
|---------|--------|
| expat | EXPAT headers missing; CMake fell back to the macOS SDK libexpat 2.7.1 (needs >= 2.8.5) |
| zlib | ZLIB headers missing; fell back to SDK zlib 1.2.12 (needs >= 1.3.2) |
| freetype | Freetype was found in /opt/homebrew (outside the environment); added so builds do not depend on the host |

xerces-c, clhep and hdf5 were found inside the environment without additions.
Linux-only dependencies are UNCLEAR until the CI run.

## Measurements

None. No performance has been measured.

## Local verification (2026-10-22, macOS arm64, Darwin)

- Clean build directory, Ninja, flags as in README: built.
- vikshep-sim info --json piped through python -m json.tool: valid; keys equal
  the sort_keys serialization; also runs with an empty environment (no Geant4
  data variables).
- vikshep-sim run: stderr "not implemented (milestone Y3)", exit code 2.
  vikshep-sim datasets: "(milestone Y6)", exit code 2.
- scripts/check-rules.sh: ok.

## Fresh-environment test (2026-10-23, macOS arm64)

A throwaway env created from environment.yml alone (no extra installs) found
EXPAT, ZLIB, XercesC, Freetype and Geant4 inside the env, built from a clean
build directory, and passed the same checks (valid JSON, run exit code 2,
runs with an empty environment). geant4 resolved to noqt_429d0ab_0 and qt6-main
was not installed. No change to environment.yml was needed. The throwaway env
was removed.

## Open questions

- Linux build string, GDML and multithreading flags, compiler and any
  Linux-only dependencies: UNCLEAR until the first CI run.
- Why conda-forge lists two noqt builds per platform: UNCLEAR.
- The ci.yml was not validated by a YAML parser or by GitHub (no parser
  available locally, nothing pushed).
- Is physics_lists meant to be the full factory list (36 names), or a curated
  subset?
