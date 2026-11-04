# Sidecar contract v1

Status: proposed. Becomes a contract when merged. Frozen after milestone Y7.

`vikshep-sim` runs as a child process ("sidecar") of the desktop app. The app
talks to it only through command-line arguments, stdout, the exit code, signals
and files. This document is that interface. It is written to be moved, unedited,
into another repository; it refers to sibling documents only by file name
(HITS.md).

Words MUST, SHOULD and MAY are used in the usual sense.

Implementation status: `info`, `validate` and `run` implement this contract.
`dev-run` (experimental) is outside it. The `datasets` command is a stub that exits
with code 2. Windows is specified but not built or tested (Decision S11).

## 1. Commands

### 1.1 `vikshep-sim info --json`

Prints one JSON object followed by a newline to stdout, exits 0. It needs no
Geant4 data sets, creates no run manager, writes nothing to stderr in normal
operation. Keys are sorted at every level; the output contains no timestamps and
no paths.

Fields emitted today:

| Key | Type | Meaning |
|-----|------|---------|
| tool_version | string | vikshep-sim version |
| geant4.version | string | Geant4 version, for example "11.4.3" |
| geant4.version_number | integer | Geant4 version number, for example 1143 |
| geant4.gdml | boolean | GDML support compiled in |
| geant4.multithreaded | boolean | multithreaded Geant4 |
| physics_lists | array of string | reference physics list names from Geant4 |
| em_options | array of string | EM option suffixes that can be appended to a physics list name; omitting the suffix selects the list's default |
| templates | array of string | names of the built-in geometry templates (`layered_calorimeter`, `tracker_barrel`); see SPEC.md |
| datasets | array | installed data sets; empty until milestone Y6 |
| platform.os | string | "linux", "macos", "windows" or "unknown" |
| platform.arch | string | "x86_64", "arm64" or "unknown" |
| platform.compiler | string | for example "clang 21.1.8" |

Also emitted since the spec milestone (they were "additions" in the first draft
of this document):

| Key | Type | Meaning |
|-----|------|---------|
| contract_version | integer | version of this document, currently 1 |
| spec_versions | array of string | `spec_version` values the binary accepts, for example ["1.0", "1.1"] |
| hits_schema_versions | array of string | `vikshep.hits.schema_version` values it can write, for example ["1.0"] |

Example (lists shortened):

```json
{
  "contract_version": 1,
  "datasets": [],
  "em_options": ["_EMV", "_EMX"],
  "geant4": {"gdml": true, "multithreaded": true, "version": "11.4.3", "version_number": 1143},
  "hits_schema_versions": ["1.0"],
  "physics_lists": ["FTFP_BERT", "FTFP_BERT_TRV"],
  "platform": {"arch": "arm64", "compiler": "clang 21.1.8", "os": "macos"},
  "spec_versions": ["1.0", "1.1"],
  "templates": ["layered_calorimeter", "tracker_barrel"],
  "tool_version": "0.1.0"
}
```

The entries of `templates` are plain names for now; richer entries may be added
later as a compatible change. The shape of entries in `datasets` is defined at
milestone Y6; until then the app MUST treat that array as an opaque list.

### 1.2 `vikshep-sim validate --spec <file> [--canonical]`

Checks a spec without running a simulation (rules: SPEC.md).

- Valid: exit 0, stdout is one line `{"event":"valid","spec_version":"<version of the spec>"}`
  (for a spec written as 1.0, `"1.0"`; for 1.1, `"1.1"`); with
  `--canonical`, stdout is instead the canonical form of the spec (SPEC.md,
  section 4) followed by a newline.
- Invalid: exit 2, stdout is exactly one `error` line as in section 2.1 with the
  `errors` array, nothing else.
- Human-readable text goes to stderr.

### 1.3 `vikshep-sim run --spec <file> --out <dir> [--threads N] [--progress jsonl]`

| Option | Meaning |
|--------|---------|
| `--spec <file>` | path to the JSON spec (simspec). Required. |
| `--out <dir>` | the output folder to create. Required. It MUST NOT exist; its parent MUST exist. |
| `--threads N` | worker threads, a positive integer. Default **1**. It never changes the output: the same spec and seed give the same `hits.arrow` and OID for any N (HITS.md, section 4). |
| `--progress jsonl` | write machine-readable events to stdout (section 2). Default is `none`: stdout stays empty, in success and in failure; the exit code and stderr tell what happened. |

Reading the spec from stdin is not supported in v1.

Mistakes in the command line itself (an unknown option, a missing `--spec`, a bad
`--progress` value) are found before `run` starts. They are reported on stderr only,
with exit code 2; stdout stays empty, because the progress mode may not be known.

Order of checks, before any output exists: the spec is validated in full (exit 2);
then `--out` (exists, or its parent does not: `OUTPUT_EXISTS`, exit 2); then the Geant4
data sets the physics list needs (`DATASETS_MISSING`, exit 3). Only then is the temporary
folder created and the first progress line written.

## 2. stdout and stderr

- In `--progress jsonl` mode stdout carries **only** JSON lines: one JSON
  object per line, compact (no newlines inside), UTF-8, terminated by `\n`,
  keys sorted, flushed after every line.
- Human-readable logs go to **stderr only**, in every mode. This includes all
  Geant4 output: the implementation MUST redirect Geant4's own output stream
  (`G4cout`, banners and warnings) to stderr, because Geant4 writes to stdout by
  default.
- How `run` guarantees this: at startup it duplicates file descriptor 1 into a private
  channel that only the progress/done/error writer uses, points descriptor 1 at stderr
  (so printf, puts and any library write to descriptor 1 go to stderr), points
  `std::cout` at stderr's buffer (Geant4 library code in worker threads writes to
  `std::cout` directly, one stray line per worker thread, bypassing `G4cout`), and routes
  `G4cout` and `G4cerr` to stderr. All lines are written from one function, one `write`
  per line, under a lock, so lines never interleave. If the reader of stdout goes away,
  further lines are dropped and the run is cancelled (exit 4).
- The app MUST NOT parse stderr. Its text is not stable.

### 2.1 Events

Progress:

```json
{"done":120,"event":"progress","total":1000}
```

`done` is the number of events completed and committed, `total` the number of
events requested. `done` never decreases and never exceeds `total`.

Done:

```json
{"event":"done","oid":"0123456789abcdef0123456789ab"}
```

`oid` is the 28-character OID of `hits.arrow` (HITS.md, section 6). The line is
emitted only after the output folder has been renamed into place (section 5).

Error:

```json
{"code":"SPEC_INVALID","errors":[{"message":"must be at least 1","pointer":"/events"}],"event":"error","message":"1 problem in spec"}
```

`code` is one of the stable codes in section 4. `message` is for people and is
not stable. The optional `errors` array (Decision S6) is present for
`SPEC_INVALID` and lists every problem found; each entry has a `pointer` (RFC
6901 JSON Pointer into the spec file) and a `message`. When the problem is a
missing required field, the `pointer` names the parent object and the entry has
one more key, `missing`, holding the name of the missing field (so the app
can build the full path itself):

```json
{"message":"missing required field 'thickness_mm'","missing":"thickness_mm","pointer":"/geometry/active"}
```

### 2.2 Cadence and ordering

- A first `progress` line with `done` = 0 is emitted when the run starts, after
  spec validation succeeds and before the first event.
- After that, at most one `progress` line every 250 ms (Decision S1).
- A final `progress` line with `done` = `total` is always emitted before `done`.
- Exactly one terminal line ends a run in jsonl mode: `done` (exit 0) or `error`
  (non-zero exit). Nothing is written to stdout after it.
- If the process dies without a terminal line (a crash, SIGKILL), the app MUST
  treat the run as failed and use the exit status or signal.

## 3. Exit codes

| Code | Meaning | Error codes |
|-----:|---------|-------------|
| 0 | success; a `done` line was emitted | none |
| 1 | simulation or internal error | GEOMETRY_ERROR, OUTPUT_NOT_WRITABLE, SIMULATION_FAILED, INTERNAL |
| 2 | bad usage or invalid spec | USAGE, SPEC_INVALID, SPEC_UNREADABLE, SPEC_NOT_JSON, SPEC_VERSION_UNSUPPORTED, OUTPUT_EXISTS |
| 3 | required Geant4 data sets missing | DATASETS_MISSING |
| 4 | cancelled | CANCELLED |

Exit 0 happens if and only if a `done` line was emitted (in jsonl mode). The
command-line parser's own failures MUST also exit 2 with code `USAGE`.
(Decision S2: this reuses 2, which the Y1 stub uses for "not implemented".)

## 4. Error codes

Stable. New codes MAY be added in later minor versions of the contract; the app
MUST handle an unknown code by falling back to the exit code.

| Code | When |
|------|------|
| USAGE | unknown option, missing required option, bad option value |
| SPEC_INVALID | the spec fails validation (section 6) |
| SPEC_UNREADABLE | the spec file cannot be opened or read |
| SPEC_NOT_JSON | the spec file is not valid JSON |
| SPEC_VERSION_UNSUPPORTED | `spec_version` is not in `spec_versions` |
| OUTPUT_EXISTS | `--out` already exists, or its parent does not |
| DATASETS_MISSING | Geant4 data sets needed by the spec's physics list are not available |
| GEOMETRY_ERROR | the geometry (template or GDML) cannot be built |
| OUTPUT_NOT_WRITABLE | creating or writing the temporary folder, or the final rename, failed |
| SIMULATION_FAILED | Geant4 reported a fatal error during the run: a G4Exception of severity FatalException, FatalErrorInArgument, RunMustBeAborted or EventMustBeAborted, on any thread. The run stops at once, exactly one error line is written (the message is short; origin, code and text of the Geant4 exception are on stderr), the temporary output folder is removed, and the process ends with exit code 1 without a core dump. Geant4 warnings (JustWarning) are written to stderr and the run goes on |
| INTERNAL | any other failure of the program itself |
| CANCELLED | the run was cancelled (section 7) |

## 5. Output folder

`--out <dir>` contains, on success:

| File | Presence | Content |
|------|----------|---------|
| hits.arrow | always | the hits (HITS.md) |
| provenance.json | always | what produced the hits (5.1) |
| run_summary.json | always | counts and the OID (5.2) |
| geometry.glb | only if the spec asks for it | glTF binary of the geometry |
| trajectories.json | only if the spec asks for it | trajectories |

The spec's field names that request the optional files are defined in the
simspec schema. Their formats are not part of v1 beyond their file names.
Only `hits.arrow` is hashed into the OID.

No other files are present. The app MUST ignore files it does not know.

### 5.1 provenance.json

Deterministic: byte-identical for the same build, platform, spec and seed. No
timestamps, no absolute paths, no host or user names, no thread count. Keys sorted.
Two-space indentation, one final newline. Guaranteed keys:

| Key | Content |
|-----|---------|
| build_type | the CMake build type of the program (`Release` unless the builder chose another); it is part of "same build" for reproducibility. It changes the bytes of `provenance.json`, not those of `hits.arrow` (the same hits give the same OID in Release and in an unoptimised build; one comparison on one machine, see STATUS.md) |
| contract_version | integer |
| data_sets | array, sorted by `env_var`, one entry per Geant4 data set the run needs: `env_var` (for example `G4LEDATA`), `directory` (base name of the data directory, which carries the data set version, for example `EMLOW8.8`), and `conda_package`, `conda_version`, `conda_build` (from the conda metadata when it can be found, otherwise the string `"unknown"`; never an error) |
| default_production_cut_mm | the default production cut in effect |
| geant4 | as in `info` |
| geant4_conda | `{"build": ..., "version": ...}` of the conda package `geant4` that provides the Geant4 libraries (for example build `noqt_429d0ab_0`, version `11.4.3`), found like the data set versions: the environment prefix is derived from the data directories, then `conda-meta/geant4-<version>-<build>.json` is looked up by file name. Both are the string `"unknown"` if it cannot be found (never an error). The build string matters because conda-forge has several builds of one Geant4 version (for example noqt builds that differ in their hdf5 dependency) |
| hits_schema_version | string |
| nanoarrow | `{"commit": <40 character commit>, "version": <version string>}` of the vendored nanoarrow (HITS.md, section 6.1) |
| physics_list | name |
| platform | as in `info` |
| run_manager | the Geant4 run manager class used |
| seed | the spec's seed |
| seeding_scheme | integer, version of the per-event seeding (HITS.md, section 4.2) |
| spec | the canonical, resolved spec as a JSON object (SPEC.md, section 4). For a spec written as 1.0 it has no `field` key; for 1.1 it has one (the magnetic field, zero or not) |
| spec_oid | the OID rule applied to the bytes of the canonical spec (SPEC.md, section 4), so the same experiment has the same `spec_oid` whatever the order of keys in the spec file |
| spec_version | string | the spec's own version as written in the file |
| tool_version | string |
| writer_scheme | integer, version of the way `hits.arrow` is written (HITS.md, section 6.1) |

Further keys MAY be added. (Decision S5.)

### 5.2 run_summary.json

Keys sorted. Guaranteed keys:

| Key | Type | Meaning |
|-----|------|---------|
| oid | string | the 28-character OID of hits.arrow |
| contract_version | integer | this contract's version |
| hits_schema_version | string | for example "1.0" |
| spec_version | string | the spec's own version as written in the file, for example "1.0" or "1.1" |
| events_requested | integer | events asked for |
| events_completed | integer | events finished (equals events_requested on success) |
| events_with_hits | integer | events with at least one hit |
| events_without_hits | integer | events with no hit |
| hits_total | integer | rows in hits.arrow |
| hits_file_bytes | integer | size of hits.arrow in bytes |
| threads | integer | worker threads used |
| wall_time_s | number | elapsed seconds |

`wall_time_s` and `threads` are in `run_summary.json` only. Neither appears in
`hits.arrow` or `provenance.json`, so nothing that is hashed or compared for
reproducibility contains a wall time. `run_summary.json` as a whole is not
reproducible byte for byte and is not hashed.

### 5.3 Atomic write

1. Create a temporary folder next to the target, in the same parent
   (name `.<basename of --out>.partial-<process id>`).
2. Write all files there and flush them.
3. Rename the temporary folder to `--out`. The rename is atomic on the same
   file system.
4. Only then emit the `done` line.

Either the whole output folder exists or `--out` does not exist. The app MUST
NOT read the temporary folder. After a kill that cannot be handled (SIGKILL, power
loss) a stale `.partial-*` folder MAY remain; the app MAY delete such folders
next to its own output paths.

## 6. Spec validation

- The spec is validated in full **before any simulation starts**, and before
  the output folder or its temporary folder is created.
- Each problem is reported with a JSON Pointer (RFC 6901) to the offending
  field; a missing required field points to its parent object and the entry
  carries a `missing` key naming the field (section 2.1).
- Validation covers syntax, `spec_version`, required fields, types, ranges,
  names (physics list, EM option, template) against what `info --json` lists,
  the gun start point against the world (SPEC.md, 2.2 and 2.5),
  and consistency between fields (for example, that the layers of a
  `tracker_barrel` have increasing radii and do not overlap, SPEC.md 2.5; each
  problem points at the array element).
- Missing data sets are a separate check, run after validation succeeds:
  `DATASETS_MISSING`, exit 3.
- Exit code 2 and no output folder.

## 7. Cancellation

- Signal: **SIGTERM** on macOS and Linux; **Ctrl-Break** (CTRL_BREAK_EVENT) on
  Windows. SIGINT is treated like SIGTERM. Windows is specified here but is not
  built or tested by the project today.
- On the signal the process: stops starting new events; lets events already
  running finish (so cancellation takes at most the time of one event);
  writes **nothing partial**: no `--out`, no file from this run; removes its
  temporary folder; emits `{"code":"CANCELLED","event":"error","message":"..."}`
  in jsonl mode; exits **4**.
- If the output folder has already been renamed into place when the signal
  arrives, the run is complete: it emits `done` and exits 0.
- How: the first SIGTERM or SIGINT sets an async-signal-safe flag. The simulation uses
  Geant4's soft abort: after each event, each worker thread checks the flag and, if it is
  set, tells its run manager to finish the current event and start no other. So the
  cancellation latency is about the time of one event (measured: about 30 ms in a run of
  5 GeV electrons with 2 threads, STATUS.md). A signal that arrives while Geant4 is still
  initialising (about a second and a half for this geometry) is acted on when
  initialisation ends; the run is not started. The same flag is set when stdout cannot be
  written any more.
- A second signal while shutting down: exit 4 at once without further output
  (Decision S4). A temporary folder MAY then remain.

## 8. Versioning of the contract

- `contract_version` (integer, in `info --json`) is 1 for this document.
- Compatible changes keep the number: new optional keys in `info`, new optional
  keys in jsonl lines, new error codes, new keys in `run_summary.json` and
  `provenance.json`.
- Incompatible changes bump it: removing or renaming a key, changing a meaning,
  a type or an exit code, or changing the cancellation behaviour.
- The app MUST read `contract_version` first and refuse a value it does not
  know. It MUST ignore unknown keys.
- Frozen after Y7.

## 9. What the app may rely on, and what it must not

The app MAY rely on:

- the `info --json` keys in 1.1 and their types;
- jsonl lines exactly as in section 2, and the terminal-line rule;
- the exit codes and error codes in sections 3 and 4;
- the output folder being complete or absent (5.3);
- `hits.arrow` being exactly as specified in HITS.md, and its OID being the
  fingerprint of those bytes;
- the same build, platform, spec and seed giving the same `hits.arrow` and OID.

The app MUST NOT:

- parse stderr, or stdout in modes other than `--progress jsonl`;
- depend on the exact `message` text or on the timing of progress lines;
- read the temporary folder or any file not listed in section 5;
- assume the order or contents of `physics_lists`, `em_options` or `templates`
  stays the same across tool versions;
- compare OIDs from different builds, platforms or operating systems and expect
  them to match, or tell a user that they should;
- treat `wall_time_s` or `threads` as reproducible.

## Decisions to confirm

| ID | Decision | Proposed default | Alternative |
|----|----------|------------------|-------------|
| S1 | Progress cadence | At most every 250 ms; first line at done = 0; final line always | Fixed event-count steps |
| S2 | Exit code 2 | Bad usage or invalid spec; the Y1 "not implemented" stub is replaced when `run` exists | Keep 2 for not implemented and move usage to another code |
| S3 | Extra keys in `info --json` | Add `contract_version`, `spec_versions`, `hits_schema_versions` (accepted; emitted since the spec milestone) | Only `contract_version` |
| S4 | Second signal | Decided: exit 4 immediately (from the signal handler), no output, temp folder may remain | Ignore further signals |
| S5 | provenance.json | Deterministic, guaranteed keys as in 5.1 | Fold into run_summary.json |
| S6 | Error line extension | Extra `errors` array of {pointer, message, missing?} on SPEC_INVALID (`missing` added in the calorimeter milestone) | One error line per problem |
| S7 | `--out` handling | Must not exist; parent must exist | Allow an existing empty folder |
| S8 | Default of `--progress` | Decided: `none` (stdout empty) | `jsonl` always |
| S9 | Sources of the spec | `--spec <file>` only | Also `-` for stdin |
| S10 | SIGINT | Decided: treated like SIGTERM | Not handled |
| S12 | Default of `--threads` | 1 (added with the `run` command) | The number of cores |
| S11 | Windows | Specified, not built or tested | Drop from the contract until built |
