# STATUS

Last updated: 2026-10-24 (UTC, from date -u).

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

## Milestone Y4: the calorimeter in Geant4 (no hits file yet)

- [x] Decisions 0a-0c: derived gun default, world box and gun-in-world check, `missing` key in errors
- [x] Geometry (src/dev_run.cc): world of G4_Galactic, stack centred at the origin, layer 0 at the
      front, absorber then active per layer, logical volumes `absorber` and `active`, copy_no =
      layer index, materials from G4NistManager. Nothing printed to stdout.
- [x] Physics list from G4PhysListFactory; G4ParticleGun per thread from the spec; run manager
      through G4RunManagerFactory with `--threads` (default 1)
- [x] Geant4 data set check before running: DATASETS_MISSING, exit 3 (src/datasets_check.cc)
- [x] `vikshep-sim dev-run --spec <file> [--threads N]`: EXPERIMENTAL, outside the sidecar
      contract, to be replaced by `run`
- [x] Comparison with Geant4 example B4a (below)
- [x] Tests: data set check, dev-run CLI, physics sanity run (own executable), gun-in-world and
      missing-key validator tests; ctest 52 of 52 passed on macOS arm64 (2026-10-23)
- [ ] hits.arrow, provenance.json, run_summary.json, `run` (next prompts)

### Run manager and production cut (for provenance later)

- Run manager type: `G4RunManagerType::MTOnly`, i.e. G4MTRunManager, chosen explicitly for any
  thread count including 1. Not the serial manager (serial and multithreaded runs seed events
  differently, so threads=1 would otherwise differ from threads=N). Not "Default" (that is
  tasking, or whatever G4RUN_MANAGER_TYPE says).
- Default production cut in effect: 0.7 mm (read from the physics list, FTFP_BERT). It is the
  Geant4 default; the spec has no field to change it.
- The master random engine is seeded from the spec's `seed`; Geant4 then seeds each event from
  the master. Observed on 2026-10-23: the example spec printed identical tables with 1 and with 4
  threads, and identical output on repeated runs. This is an observation, not a guarantee; how
  events are seeded is the B4 question in docs/notes-B4.md and is not answered here.
- Geant4 output: `G4cout` goes to stderr through a G4UIsession, and file descriptor 1 is also
  redirected to stderr for the duration of the run, because a cross-section table line reached
  stdout without passing through `G4cout`. Verified: stdout of dev-run holds only the table, with
  1 and 4 threads.

### Geant4 data sets (findings, 2026-10-23)

- How they arrive: the conda-forge `geant4` package depends on one conda package per data set
  (12 `geant4-data-*` packages here). They are installed under
  `$CONDA_PREFIX/share/Geant4/data` and activating the environment sets one variable per data set
  (G4LEDATA, G4PARTICLEXSDATA, G4ENSDFSTATEDATA, G4LEVELGAMMADATA, G4RADIOACTIVEDATA,
  G4NEUTRONHPDATA, G4PIIDATA, G4SAIDXSDATA, G4ABLADATA, G4INCLDATA, G4REALSURFACEDATA,
  G4CHANNELINGDATA).
- Present in this environment: yes, all 12. Nothing was downloaded (so the 5 GB question did not
  arise). No data is stored in the repository.
- Experiment (macOS arm64, FTFP_BERT, example spec): with a variable pointing at an empty
  directory, the run aborts (exit 134, G4Exception) for G4LEDATA (em0003), G4PARTICLEXSDATA
  (had014) and G4ENSDFSTATEDATA (PART70001). For G4LEVELGAMMADATA, G4SAIDXSDATA, G4PIIDATA and
  G4RADIOACTIVEDATA the run completed normally, also with 200 protons of 5 GeV; the unset
  G4NEUTRONHPDATA, G4ABLADATA and G4INCLDATA also did not matter for FTFP_BERT.
- What the check requires: G4EMLOW, G4PARTICLEXS, G4ENSDFSTATE and PhotonEvaporation for every
  list, plus G4NDL for names containing "HP" or "LEND", plus G4INCL and G4ABLA for "INCLXX".
  PhotonEvaporation is kept in the list because Geant4 documents it for hadronic physics, although
  the runs above did not need it: UNCLEAR whether FTFP_BERT can need it in rarer events.
  Whether SAID, PII and RadioactiveDecay are needed by other lists: UNCLEAR (not tested).
- Missing data sets give code DATASETS_MISSING, exit 3, and a message naming the data set, the
  variable and the `micromamba install` command. Setup steps are in README.md.

### Comparison with Geant4 example B4a (v11.4.3 source, 2026-10-23)

Setup: 1 GeV e-, 1000 events, FTFP_BERT, 10 layers of 10 mm Pb + 5 mm liquid argon, 100 mm wide,
4 threads each. B4a built unmodified from ~/geant4-src (sparse clone of tag v11.4.3, outside this
repository), per-event values read from its ntuple (B4.root). Ours: `dev-run` with seed 42.
Mean energy deposited per event, MeV, +- standard error of the mean (1000 events):

| Quantity | B4a | vikshep-sim | Difference (ours - B4a) | In sigma |
|----------|-----|-------------|-------------------------|----------|
| absorber (Pb) | 904.075 +- 0.884 | 904.731 +- 0.694 | +0.656 +- 1.124 | 0.6 |
| gap / active (lAr) | 48.773 +- 0.316 | 48.858 +- 0.325 | +0.085 +- 0.453 | 0.2 |
| total | 952.849 +- 0.862 | 953.589 +- 0.647 | +0.740 +- 1.078 | 0.7 |

The two agree within statistical error. Differences between the setups, all left as they are
(our code was not adjusted):

- Liquid argon: B4 defines its own material, Z = 18, A = 39.95 g/mole, density 1.390 g/cm3
  (radiation length 140.6 mm); ours is G4_lAr from the NIST manager, density 1.396 g/cm3
  (radiation length 140.0 mm). Same mean excitation energy, 188 eV. The 0.4 % density difference
  is far below the statistical error of the gap value.
- World: B4 is 1.2 times the calorimeter (120 mm x 120 mm x 180 mm); ours is 300 mm x 300 mm x 350 mm.
  Both are vacuum, so this only matters for particles that leave the calorimeter.
- Gun position: B4 starts at z = -90 mm, the edge of its world (15 mm in front of the stack); ours
  at z = -85 mm (10 mm in front). Both in vacuum.
- Vacuum: B4 builds its own "Galactic" (Z = 1, A = 1.01 g/mole, universe mean density); ours is
  G4_Galactic (1e-25 g/cm3).
- Random seed: B4 uses Geant4's default seed, ours 42, so the samples are independent.
- Run manager: B4 uses the factory default; ours is G4MTRunManager (section above).
- Layers: B4 uses a replica of one layer volume, ours places each slab separately. Same positions
  (absorber first, then gap, per layer).
- Scoring: B4 sums energy in the stepping action by physical volume; ours uses a sensitive detector
  on each slab type. Both sum G4Step::GetTotalEnergyDeposit.
- B4a's printed histogram means exclude overflow (its histograms end at 330 MeV and 30 MeV), so
  the printed EAbs/EGap lines are not usable at 1 GeV; the ntuple was used instead.

### Physics sanity test threshold

tests/test_dev_run.cc runs 20 events of 1 GeV e- and requires: every event's total deposit > 0 and
<= the incident energy (tiny rounding allowance), mean contained fraction > 0.8, and more energy
in the absorber than in the active slabs. Measured mean fractions for seeds 1 to 6 (20 events
each, 2026-10-23): 0.953, 0.959, 0.958, 0.947, 0.940, 0.953 (standard errors 0.003 to 0.016), and
0.954 +- 0.001 for the 1000-event run. 0.8 is about 0.14 below the lowest measurement, loose
enough not to fail on a statistical fluctuation, tight enough to fail if the stack were missing or
mostly vacuum. When data sets are missing the test is reported as skipped, with a message.

### Contract changes in this milestone

- SPEC.md: the default `gun.position_mm` is derived, [0, 0, -(stack_length/2 + 10)] mm (was fixed
  [0, 0, -20]). The canonical form of a spec without a position therefore changes (the example now
  canonicalises with z = -85), and so does any hash of it.
- SPEC.md and validator: the world box is defined (transverse_mm + 200 mm, stack_length + 200 mm,
  G4_Galactic); the gun start must lie strictly inside it (pointer /gun/position_mm). Decision P1
  is resolved in favour of a centred stack with a derived default.
- SIDECAR.md and validator: a missing-field error keeps the parent pointer and gains a `missing` key
  naming the field. Changes the shape of `errors` entries (additive).
- HITS.md section 5.1: coordinate frame of the layered_calorimeter template added (centred stack,
  layer 0 at the front, absorber first).
- spec.schema.json: the position default annotation was removed; the gun-in-world rule cannot be
  expressed in JSON Schema and is checked by the validator only.
- Not a contract change but worth knowing: command-line usage errors exit 2 (since the previous
  milestone); `dev-run` exit codes are 0, 1, 2, 3 as in SIDECAR.md.

### Open at the end of Y4 (UNCLEAR)

- Linux has not been built or run: dev-run, the data set check and all tests are verified on macOS
  arm64 only. CI will be the first Linux run.
- The data set requirement table is partly empirical (see above).
- Thread-count independence of event seeding (docs/notes-B4.md) is observed, not understood.
- dev-run keeps all per-event energies in memory (events x 2 x layers doubles); fine for the
  event counts used here, not designed for 10 million events.

## Milestone Y5: per-event seeding and the hits sensitive detector (no hits.arrow yet)

- [x] Seeding measured (tools/seedprobe, docs/notes-B4.md) and made explicit: every event reseeds
      its thread's engine from a hash of (spec seed, global event id); hash in HITS.md 4.2
- [x] Hits sensitive detector honouring `sensitive` (default ["active"]); rows as in HITS.md
- [x] Reorder buffer (src/reorder_buffer.hh), wired into the run; events leave in event_id order
- [x] `dev-run --event-digests <file>` (EXPERIMENTAL, outside the sidecar contract)
- [x] Tests a-g (below); ctest 80 of 80 passed on macOS arm64 (2026-10-23)
- [ ] hits.arrow writer, provenance.json, run_summary.json, `run` (next prompts)

### Seeding: what was measured (macOS arm64, 2026-10-23)

Details and the command to re-run are in docs/notes-B4.md (marked "measured by an automated
coding agent, to be re-run by Yash"). Short version, for G4MTRunManager with default settings:
SeedOncePerCommunication = 0 (seeds per event), the worker sees the global event id, the engine
state of event k depends only on (master seed, k): the same with 1 and 4 threads, in runs of 5, 20,
200, 10010, 20000 and 25000 events (across the seed refill at 10000), and the tasking manager gives
the same states. The serial manager does not: it gave different states from the MT manager with 1
thread. So the default MT behaviour was already thread-independent, but only because of how
Geant4 draws its seeds; the explicit reseeding makes it hold by construction. Not tested:
SetEventModulo, SetSeedOncePerCommunication, a field, Linux.

### Hits: what is recorded

- One row per G4Step in a sensitive volume with edep > 0 (decision D3), as `Hit` in src/hits.hh:
  volume (slab type), copy_no, layer (= copy_no for this template), x y z (step midpoint, mm),
  t (pre-step global time, ns), edep (MeV), px py pz (pre-step momentum, MeV/c), track_id,
  parent_id, pdg, step (G4Track::GetCurrentStepNumber, 1-based). Units are those of HITS.md;
  nothing is converted later.
- Rows of an event are collected in a thread-local buffer, in step order, and handed to the reorder
  buffer when the event ends.
- dev-run's energy table still covers both slab types whatever `sensitive` says.
- Measured, 2026-10-23: the 5 GeV example with both volumes sensitive gives 12103 to 13227 hits per
  event (mean 12621; absorber 11237, active 1384, over 20 events). With the default `sensitive`
  (active only) about 1250. Relevant for the writer: a Hit is about 100 bytes in memory, so an event
  with both volumes is about 1.3 MB; the reorder window of dev-run is 16 * threads + 16 events
  (about 100 MB at 4 threads in the worst case with such events).

### Reorder buffer (src/reorder_buffer.hh)

- Accepts events in any order from any thread, calls the consumer strictly in event_id order, never
  from two threads at once. The consumer runs on the submitting thread that completes the next id.
- Memory: unbounded by default (never blocks, cannot deadlock, but a slow event n keeps all later
  events in memory). With `max_ahead = k`, `submit(id)` waits while `id >= next + k`, so at most k
  events are held. The next-needed event is never made to wait, so progress is always possible,
  provided events are started in increasing id order, which G4MTRunManager does (measured: blocks
  of consecutive ids, in order). A caller that submits a high id before a low id it also owns would
  wait for itself; the class comment says so. dev-run uses a window of 16 * threads + 16.
- A consumer that throws marks the buffer failed: the exception reaches the thread that ran the
  consumer, all other submitters (waiting or later) get an exception, nobody is left waiting. In
  dev-run the first exception is kept, the run finishes without further work, and it is rethrown.
- Tested with scrambled and interleaved submissions (8 threads), a slow first event (unbounded and
  bounded), the window, duplicate ids and a failing consumer; the group was run 10 times in a row
  without a failure.

### Experimental digests (`dev-run --event-digests <file>`)

One JSON line per event, in event order, keys: edep_by_volume_MeV, edep_sum_MeV, event_id, hash,
hits, hits_by_volume (entries for the sensitive volumes). hash = FNV-1a 64 bit over the rows in step
order; each row contributes volume, copy_no, layer, then x y z (quantised to 1e-6 mm), t (1e-6 ns),
edep (1e-9 MeV), px py pz (1e-6 MeV/c), then track_id, parent_id, pdg, step, all as little-endian
integers (a quantised value is llround(value / quantum)). Events with no hits get a line with hits 0
and hash cbf29ce484222325 (the FNV offset basis). Defined in src/event_digest.hh; marked
experimental in --help.

To compare 1 and 4 threads yourself (from the repository root, environment active, build as in
README.md):

    ./build/vikshep-sim dev-run --spec docs/examples/calorimeter.json --threads 1 --event-digests /tmp/d1.jsonl > /dev/null 2>&1
    ./build/vikshep-sim dev-run --spec docs/examples/calorimeter.json --threads 4 --event-digests /tmp/d4.jsonl > /dev/null 2>&1
    cmp /tmp/d1.jsonl /tmp/d4.jsonl && echo identical

### Tests added

a. reorder buffer unit tests (tests/test_reorder_buffer.cc). b. 1 thread against 4 threads: every
digest line identical. c. events 0-4 of a 5-event run equal events 0-4 of a 20-event run. d. a
different seed gives different hashes for every event compared. e. in-process (tests/test_dev_run.cc,
both volumes sensitive): hit rows are well formed (inside their slab, ids and steps sane, steps
increasing within a track, events delivered once and in order) and the summed hit energy per volume
equals the energy table of the same run within 1e-9 relative. f. sensitive ["absorber"] gives hits only
in the absorber (and the default gives only active). g. an electron aimed along +x leaves the world
without touching the stack: all events are zero-hit and every one still gets a digest line with count 0.
Also: seed hash golden values computed independently in Python (tests/test_event_seed.cc), digest unit
tests, and an unwritable digest file gives exit code 1. All the Geant4 tests report "Skipped" with a
message when data sets are missing (checked with G4LEDATA unset: 11 skipped, none passed silently).
A mutation check (seed made to depend on the thread id) made tests b and c fail, so they can detect
what they test.

### Data set versions for provenance (for the writer prompt)

Confirmed, 2026-10-23: the conda metadata of each data set package is in the environment's
`conda-meta` folder, `conda-meta/geant4-data-<name>-<version>-<build>.json`. Each file has name,
version, build, channel, url, sha256 and size of the package (checked for all 12: for example
geant4-data-emlow 8.8.0 hd8ed1ab_0, geant4-data-particlexs 4.2.0, geant4-data-ensdfstate 3.0,
geant4-data-photonevaporation 6.1.2, all from conda-forge). The data directories themselves carry
the data set version in their names (share/Geant4/data/EMLOW8.8, PARTICLEXS4.2, ...), which is what
Geant4 actually reads.

How the tool can read them at run time (not implemented): for each required data set take the value
of its environment variable (already done by the data set check, src/datasets_check.cc), record the
directory's base name (the data set name and version, available without conda), and, if present, find
the conda record by going four directories up from the data directory to the environment prefix
(prefix/share/Geant4/data/<dir>) and reading prefix/conda-meta/geant4-data-*.json; record its name,
version, build and sha256. Do not rely on CONDA_PREFIX (it can be unset); do not hash the data
directories at run time (EMLOW has 11432 files). The conda version differs a little from the directory
name (8.8.0 against EMLOW8.8), so record both. A non-conda installation has no conda-meta, so the
directory name must be enough; provenance should say which source it used.

### Contract and document changes in this milestone

- HITS.md: new section 4.2 (event seeding: the hash, the engine, the two seed sizes, reference values),
  and section 4 no longer calls thread independence an open question. Changing the hash changes every
  OID: an incompatible change.
- notes-B4.md: both questions filled in (see above).
- No change to SIDECAR.md, SPEC.md or the schema.
- dev-run gained `--event-digests` (experimental).

### Open at the end of Y5 (UNCLEAR)

- Everything above was run on macOS arm64 only; Linux is untested until CI.
- Behaviour with other run manager settings is untested (see Seeding).
- The probe and the notes are measurements by an automated agent; the owner has not re-run them.
- dev-run holds each event's hits until released (bounded by the window, see above); the writer will
  need to stream.
- The notes mark the measurements as made by an automated coding agent. The exact wording first asked
  for could not be used: scripts/check-rules.sh rejects the product name in files (rule: no AI
  attribution in files).

## nanoarrow spike (2026-10-24, Y6 step 1): decided, option 1

Question: can nanoarrow's IPC component write hits.arrow as docs/HITS.md describes it? Spike program
and checks: tools/nanoarrow_spike (outside the build). Two versions of the same library were tried;
both were bundled with nanoarrow's own `ci/scripts/bundle.py --with-ipc --with-flatcc` and checked
with pyarrow 25.0.0 (macOS arm64).

| Criterion | nanoarrow 0.9.0 (release, 2026-07-31) | nanoarrow main@41c78c47 ("0.10.0-SNAPSHOT", 2026-10-24) |
|-----------|----------------------------------------|---------------------------------------------------------|
| writes the HITS.md schema with a dictionary-encoded `volume` | **FAILS**: `ArrowIpcWriterWriteArrayView` returns 45 "Cannot encode dictionary arrays"; the release has no function that writes a dictionary batch | passes (`ArrowIpcWriterWriteDictionaryBatch`, added 2026-09-08, file support fixed 2026-10-21) |
| pyarrow opens it: schema, dictionary, values | not reachable (a schema without the dictionary column does write and read) | passes: 16 columns in order, uint64/int32/float64 types, `dictionary<int32, utf8>` with ["absorber", "active"], non-nullable, field metadata `unit` (mm, ns, MeV, MeV/c), schema metadata `vikshep.hits.schema_version = "1.0"`, 200000 rows in batches 65536, 65536, 65536, 3392 |
| two writes byte-identical | n/a | passes: three writes (two seconds apart) are the same 20006370 bytes |
| no timestamps or other non-deterministic metadata | n/a | passes: the only printable strings in the file are the column names, `unit`, the unit values, the schema metadata key and value, and the dictionary values |
| zero-batch file opens in pyarrow | n/a | passes: 0 batches, 16 columns, 0 rows; the file holds the schema and the dictionary message |
| dictionary written exactly once | n/a | passes: messages are schema, dictionary, 4 record batches; pyarrow stats: 1 dictionary batch, 0 deltas, 0 replaced |
| buffer alignment (D5) | n/a | 8 bytes (a 101-row batch body is exactly 10128 bytes = 7 int32 columns padded to 408 bytes plus 9 8-byte columns), no compression, zero-length validity buffers, no nulls |

Further findings on `main`:

- It writes **no end-of-stream marker** before the footer unless asked. The format description puts
  `0xFFFFFFFF 00000000` there. pyarrow reads the file anyway (it uses the footer), but a stricter
  reader might not. Calling `ArrowIpcWriterWriteArrayView(writer, NULL)` before
  `ArrowIpcWriterFinalizeFile` writes it; with it, every check above still passes and the streaming
  message reader stops cleanly at the marker.
- The `main` code was released nowhere; it is 39 commits after 0.9.0. The dictionary writer is about
  seven weeks old, and the file-writing fix for dictionaries three days old. Its API may still change.

DECISION (2026-10-24): option 1 was chosen by the owner: nanoarrow `main` at commit
41c78c476817d1d122acc59ddacb7a3cbd00644c is vendored in third_party/nanoarrow, with safeguards
(end-of-stream marker, golden-file test, nanoarrow version, commit and writer_scheme in
provenance.json). The options as they were put:

1. Pin nanoarrow `main` at commit 41c78c476817d1d122acc59ddacb7a3cbd00644c (tarball sha256
   418bd978ad204d57972ec8b32369a9a12071b0d2b7adb2f15ec83db819e2a5dc), same library, unreleased. All
   criteria pass. Risk: an unreleased snapshot with a young code path; the version recorded in
   provenance would be "0.10.0-SNAPSHOT" plus the commit.
2. Wait for the nanoarrow release that contains the dictionary writer, and pin that.
3. Stay on 0.9.0 and write the missing dictionary-batch message ourselves with its encoder pieces.
   Not tried; it means maintaining encoder code that upstream already has on `main`.
4. A different writer (for example a small purpose-built one, or Arrow C++). Not evaluated; needs the
   owner's approval first.
5. Change HITS.md so that `volume` is a plain utf8 column (decision D6's alternative). 0.9.0 would then
   work, at the cost of repeating the name in every row (not measured) and a format change before it
   is frozen.


## Milestone Y6: hits.arrow, OID, provenance.json, run_summary.json, output folder

- [x] Per-event reseed moved to the start of GeneratePrimaries (nothing random can run before it);
      pinned seed values unchanged; a test-only gun that draws a random number gives the same number
      for 1 and 4 threads (tests/check_random_gun.py)
- [x] nanoarrow vendored at a pinned commit (third_party/nanoarrow, README there), Apache-2.0 licences
      and the bundled flatcc licence kept, credited in NOTICE
- [x] SHA3-256: tiny_sha3 (MIT) vendored in third_party/tiny_sha3, official test vectors pass
- [x] Hits writer (src/hits_writer.cc): streams rows into 65536-row batches, dictionary written once,
      end-of-stream marker, hash computed while writing and checked by reading the file back
- [x] provenance.json and run_summary.json (src/run_outputs.cc); data set versions from conda metadata
- [x] Atomic output folder (src/run_pipeline.cc); `dev-run --out <dir>` (EXPERIMENTAL)
- [x] Tests a to i, golden files, pyarrow checks; ctest 119 of 119 passed on macOS arm64, also in a throwaway
      environment created from environment.yml alone (2026-10-24)
- [ ] `run` command (the same pipeline function, with the sidecar contract around it)
- [ ] CI run (nothing is pushed), Linux

### What the writer does, and the safeguards

- Writer scheme 1 = nanoarrow main@41c78c47 used as in src/hits_writer.cc: dictionary batch written
  once after the schema, record batches of 65536 rows, end-of-stream marker written explicitly before the
  footer, 8-byte alignment (measured), no compression, no nulls, zero-length validity buffers.
- Golden-file test (tests/test_hits_writer.cc): a fixed dataset (10 rows in batches of 4, both volumes, two
  hit-free events) and a file with no hits are compared byte for byte with tests/golden/*.arrow. The failure
  message says that a writer change alters every OID and requires bumping writer_scheme, and how to
  regenerate. The golden files are also read by tests/check_golden.py with pyarrow, which recomputes
  the expected values from the dataset independently, and their OIDs come from hashlib.sha3_256.
- provenance.json records nanoarrow.version, nanoarrow.commit and writer_scheme (HITS.md 6.1).
- Memory: a batch of 65536 rows is about 6.5 MB, built in place and written when full; with the reorder
  window (16 x threads + 16 events) memory is bounded by the window plus one batch.
- Hit-free events are absent from the file and counted (events_without_hits).

### Measurements (2026-10-24, macOS arm64, 4 threads, example spec with 1000 events, default sensitive ["active"])

- hits: 1,331,469 rows in 21 record batches, 133,168,522 bytes: **133.2 MB per 1000 events** (about
  100 bytes per row, about 1331 rows per event). OID a80a67e6474b21bda7066264e2a9.
- Wall time: 4.19 s without --out, 7.67 s with --out (this build has no optimisation flags, see below).
  tiny_sha3 hashes about 239 MiB/s at -O2 (127 MiB in 0.53 s, one pass); the hash is computed twice (while
  writing, and when verifying), and writing and hashing happen in the single consumer thread. Python's
  hashlib needs 0.13 s for the same file. Not tuned.
- Build type: the default build has no CMAKE_BUILD_TYPE, so no optimisation. A Release build gave the
  **identical OID** for the same spec and seed on this machine (a80a67e6474b21bda7066264e2a9) and ran
  faster: 3.21 s without --out and 4.65 s with --out. One comparison on one machine, not a guarantee that
  compiler flags never change the bytes. Whether to make Release the default is left to the owner.

### Contract and document changes in this milestone

- HITS.md: D5 decided (8 bytes, measured); end-of-stream marker rule; new section 6.1 (nanoarrow version,
  commit, writer_scheme, seeding_scheme in provenance.json); 4.2 notes that reseeding is the first step of
  generating the primaries.
- SIDECAR.md 5.1: provenance.json keys listed in full. `spec_oid` is now defined as the OID of the
  canonical spec (it said: of the bytes of the spec file), to agree with SPEC.md section 4. This is a
  change to the contract text; nothing produced it before.
- run_summary.json is as in SIDECAR.md 5.2. It holds wall_time_s and threads, so the tests compare it
  without those two keys (the prompt asked for byte-identical run_summary.json; that cannot hold with them).
- NOTICE credits nanoarrow, flatcc and tiny_sha3.
- environment.yml: pyarrow added (used by the tests only).

### Notes for the next prompt (`run`)

- run_to_folder() (src/run_pipeline.hh) is the function `run` should call. It throws OutputError with
  code OUTPUT_EXISTS / OUTPUT_NOT_WRITABLE; dev-run maps OUTPUT_EXISTS to exit 2 and prints an error line.
- Not done: progress lines (jsonl), SIGTERM cancellation, optional geometry.glb and trajectories.json,
  the DATASETS_MISSING check inside the pipeline (dev-run does it before calling it).
- The temporary folder is ".<name>.partial-<pid>" next to --out. A process killed by SIGKILL leaves it
  behind (documented in SIDECAR.md 5.3). The final rename checks twice that the target is absent; a
  window of a few microseconds remains between the check and the rename.
- The reorder-buffer consumer runs the writer on a worker thread, one event at a time.

### Open at the end of Y6 (UNCLEAR)

- Linux and CI: nothing here has run on Linux. The golden files assume IEEE doubles and
  little-endian; tiny_sha3 has a big-endian path that was not exercised.
- The vendored nanoarrow is an unreleased snapshot; when a release containing the dictionary writer
  appears, switching is a writer_scheme change if the bytes differ (the golden test will say).
- pyarrow is the only reader the output was checked with.

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

- 2026-10-23: B4a comparison, see the Y4 section. Physics only; no performance has been measured.
- 2026-10-23: mean contained fraction of 1 GeV electrons, see the Y4 section.

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

- CORRECTION (2026-10-24): the three bullets above about `ArrowIpcWriterWriteDictionaryBatch()` and
  `ArrowIpcWriterWriteArrayViewWithDictionaries()` were read from the header on nanoarrow's `main`
  branch, not from the 0.9.0 release. Those two functions do **not** exist in 0.9.0. See "nanoarrow
  spike" below. (StartFile and FinalizeFile are in 0.9.0; that part was right.)
- Dictionaries (as found on `main`, not in 0.9.0): `ArrowIpcWriterWriteDictionaryBatch()` writes one
  dictionary batch; in file mode it returns ENOTSUP for a delta or a dictionary id written before.
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
