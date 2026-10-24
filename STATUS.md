# STATUS

Last updated: 2026-10-23 (UTC, from date -u).

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

## Milestone Y3 (first part): spec v1, validator, validate subcommand

- [x] docs/SPEC.md and docs/examples/calorimeter.json
- [x] docs/spec.schema.json (JSON Schema 2020-12), checked against the validator by
      tests/check_schema.py (74 cases, run by ctest)
- [x] Validator (src/spec_validate.cc): reports all errors as {pointer, message}
- [x] Canonical form (src/spec_canonical.cc): defaults resolved, direction normalised,
      sorted keys, ECMAScript number format (RFC 8785)
- [x] vikshep-sim validate --spec <file> [--canonical]
- [x] Catch2 tests wired into ctest (37 tests); CI runs ctest
- [x] info --json: templates, spec_versions, contract_version, hits_schema_versions
- [ ] Geant4 run, hits.arrow writer, provenance.json, run_summary.json (next prompts)

Verified on 2026-10-23, macOS arm64: build from a throwaway env created from
environment.yml alone (catch2 3.16.1, jsonschema 4.26.0 added), ctest 37 of 37
passed, info --json valid and byte-stable between two runs. Not verified: Linux,
and the CI run (nothing is pushed).

### Geant4-dependent checks

All three needed no full initialisation, so nothing is deferred:

- Materials: G4NistManager::GetNistMaterialNames() lists the 309 NIST names
  without building any material.
- Physics lists: names from G4PhysListFactory (AvailablePhysLists and
  AvailablePhysListsEM). A test compares the validator's answer with
  G4PhysListFactory::IsReferencePhysList for every list and every list plus EM
  suffix (and a few bad names); they agree.
- Particles: fixed allowlist in the validator, no Geant4.

### Left for the run (next prompt)

- Whether gun.position_mm lies inside the simulated world (needs the world
  size, which the template does not define yet).
- DATASETS_MISSING check for the data sets the physics list needs.
- Duplicate keys in a spec file are not detected (the last one wins); a
  stricter parser would be needed.
- Geant4 output (G4cout) redirected to stderr, required by SIDECAR.md.

### Changes to the contract documents made in this milestone

- SIDECAR.md: spec_version and spec_versions are strings ("1.0"), not integers;
  added the `validate` command; added error codes SPEC_UNREADABLE and
  SPEC_NOT_JSON (exit 2); info now emits contract_version, spec_versions,
  hits_schema_versions; `templates` entries are plain names.
- Command-line usage errors now exit 2 (before: CLI11's own codes), as
  SIDECAR.md section 3 says. --help still exits 0.

### Decisions to confirm (this milestone)

See the table at the end of docs/SPEC.md (P1 to P7). Most important:

- P1: the stack is centred on the origin (as HITS.md says), so the default gun
  start [0, 0, -20] lies inside any stack longer than 40 mm, including the
  example (150 mm). Alternative: front face of the stack at z = 0, which needs
  HITS.md section 5.1 amended.
- P2: required fields are geometry, gun.particle, gun.energy_GeV, events, seed
  (spec_version and template as specified). seed has no default.

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
- osx-arm64 has two noqt builds, differing only in the hdf5 dependency
  (noqt_d9c327f_0: hdf5 >=2.2.0; noqt_429d0ab_0: hdf5 1.14.6). The solver chose
  noqt_429d0ab_0. The pin does not fix the hdf5 line, so a future solve may pick
  the other build. Linux has two noqt builds too; whether they differ the same
  way is UNCLEAR.
- ci.yml parses as YAML and passes actionlint 1.7.12 (checked in a throwaway
  env). It has not run on GitHub.
- Is physics_lists meant to be the full factory list (36 names), or a curated
  subset? (Kept as the full list; the validator accepts any of them.)
- Linux: tests (Catch2, jsonschema, ctest) not run on Linux yet; the first CI
  run is the first check.
- Arrow IPC file writing with nanoarrow (checked 2026-10-24, answer below).
- Repo placement: this is its own repository, while the desktop app plan
  assumes the simulator lives in a `sim/` folder of the main Vikshep
  repository. docs/HITS.md and docs/SIDECAR.md are written to move there
  without edits: they name sibling documents by file name only and mention no
  repository paths, branch names or tooling of this repo. Unconfirmed: which
  repository is the home in the long run.
- Event seeding per thread and thread-count independence of the output: see
  docs/notes-B4.md. No answer yet.
- Decisions awaiting the owner: tables "Decisions to confirm" at the end of
  docs/HITS.md (D1 to D10) and docs/SIDECAR.md (S1 to S11).

## nanoarrow and the Arrow IPC file format (2026-10-24)

Question: can nanoarrow write an Arrow IPC file with a footer, or only a stream?

Finding: it can write a file. The IPC extension (nanoarrow_ipc.h) has
`ArrowIpcWriterStartFile()`, documented as "Writes the Arrow IPC magic and sets
the writer up to track written blocks", and `ArrowIpcWriterFinalizeFile()`,
documented as "Writes the IPC file's footer, footer size, and ending magic".
Both are present in the headers of tags 0.7.0, 0.8.0 and 0.9.0. The newest
nanoarrow on conda-forge at this date is 0.9.0 (GitHub release dated
2026-07-31).

Relevant details from the same header:

- Dictionaries: `ArrowIpcWriterWriteDictionaryBatch()` writes one dictionary
  batch. In file mode it returns ENOTSUP for a delta or for a dictionary id
  that was already written ("IPC files do not support dictionary replacement").
  This agrees with the single, non-delta dictionary in HITS.md section 1.2.
- `ArrowIpcWriterWriteArrayViewWithDictionaries()` writes the dictionaries
  before every batch, so in file mode only one batch can be written that way.
  Writing many batches presumably means writing the dictionary once with
  `WriteDictionaryBatch()` and the index batches with `WriteArrayView()`.
  UNCLEAR: not tried; it is an inference from the header comments.
- Compression: the extension lists ZSTD and LZ4 codecs; HITS.md requires none.

UNCLEAR, not tested: whether the bytes it produces meet every rule in HITS.md
(8-byte alignment, zero-length validity buffers, field metadata, no extra
footer metadata), and whether they are identical between runs. A Y3 spike
should write the worked example and compare two runs byte for byte before the
format is frozen.

Sources:

- https://arrow.apache.org/nanoarrow/latest/reference/ipc.html
- https://github.com/apache/arrow-nanoarrow/blob/main/src/nanoarrow/nanoarrow_ipc.h
- https://github.com/apache/arrow-nanoarrow/tree/apache-arrow-nanoarrow-0.9.0
- conda-forge package listing (`micromamba search nanoarrow`)

Because nanoarrow can write a file, no alternative writer is needed on current
evidence. If the spike above fails a rule in HITS.md, the options are: a
nanoarrow IPC extension patch, or a different small C++ writer. No choice is
made here.
