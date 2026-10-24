# Hits format v1 (hits.arrow)

Status: proposed. Becomes a contract when merged. Frozen after milestone Y3.

This document defines the file `hits.arrow` written by `vikshep-sim run`. It is
written to be moved, unedited, into another repository. It refers to sibling
documents only by file name (SIDECAR.md, notes-B4.md).

Words MUST, SHOULD and MAY are used in the usual sense.

## 1. File

- Format: Apache Arrow IPC **file** format (the one with the `ARROW1` magic at
  both ends and a footer), not the stream format.
- Metadata version: V5. Byte order: little endian.
- Compression: none. Record batch bodies are not compressed.
- Alignment: every buffer and every message is padded to a multiple of 8 bytes.
  (Decision D5: confirm against the chosen writer in Y3.)
- Nothing that varies between runs is written: no timestamps, no host or user
  names, no paths, no writer or library version strings, no random identifiers.
  The only custom metadata is the set listed in section 3.
- Nullability: all fields are non-nullable. No column contains nulls. Validity
  buffers have length 0 and `null_count` is 0 in every batch.

### 1.1 Batch-size rule

The batch size changes the bytes, so it is part of the format.

- Every record batch has exactly **65536 rows** (2^16), except the last batch,
  which has the remaining 1 to 65535 rows.
- Batches are cut by row count only. An event MAY span two batches; a batch MAY
  hold several events. Batch boundaries carry no meaning for readers.
- No record batch has 0 rows. A run with no hits at all has **zero record
  batches** (section 5.2).

### 1.2 Dictionary rule

The dictionary also changes the bytes, so it is part of the format.

- The `volume` column is dictionary-encoded with one dictionary, dictionary id
  `0`, index type `int32`, value type `utf8`, `isOrdered = false`.
- The dictionary contains the name of **every sensitive volume in the
  geometry**, whether or not it was hit, each name once.
- Values are sorted in ascending order by their UTF-8 bytes (bytewise, no locale).
  An index is the position of the name in that sorted list, starting at 0.
- The dictionary is written once, as a single non-delta dictionary batch,
  before the first record batch (or, if there are no record batches, directly
  after the schema). It is never replaced and never extended (`isDelta = false`
  and no later dictionary batch).
- Because the dictionary depends only on the geometry, it does not depend on
  which events happened to hit which volumes.

## 2. Columns

Columns appear in exactly this order, with exactly these names and types.

| # | Name | Arrow type | Unit | Meaning |
|---|------|-----------|------|---------|
| 1 | event_id | uint64 | none (index) | 0-based event index, see 4.1 |
| 2 | volume | dictionary<int32, utf8> | none (name) | name of the sensitive physical volume |
| 3 | copy_no | int32 | none (index) | copy number of that physical volume |
| 4 | layer | int32 | none (index) | layer index; -1 when the geometry has no layers or the volume is not in a layer |
| 5 | x | float64 | mm | step midpoint, x |
| 6 | y | float64 | mm | step midpoint, y |
| 7 | z | float64 | mm | step midpoint, z |
| 8 | t | float64 | ns | global time at the start of the step (pre-step point) |
| 9 | edep | float64 | MeV | energy deposited in the step |
| 10 | px | float64 | MeV/c | momentum x at the start of the step (pre-step point) |
| 11 | py | float64 | MeV/c | momentum y at the start of the step |
| 12 | pz | float64 | MeV/c | momentum z at the start of the step |
| 13 | track_id | int32 | none (index) | Geant4 track ID, 1-based within the event |
| 14 | parent_id | int32 | none (index) | track ID of the parent, 0 for a primary |
| 15 | pdg | int32 | none (code) | PDG Monte Carlo particle code as given by Geant4, 0 if Geant4 has none |
| 16 | step | int32 | none (index) | 1-based step number within the track |

Definitions:

- **Hit.** One Geant4 step inside a sensitive volume with `edep > 0`. One hit is
  one row. (Decision D3.)
- **Step midpoint.** The arithmetic mean of the pre-step and post-step
  positions, per axis, computed in float64.
- **Values.** Written exactly as Geant4 reports them in its internal units
  (mm, ns, MeV), with no conversion. NaN and infinity MUST NOT appear; the
  simulator treats one as an internal error and writes no output.

### 2.1 Units in names

The project rule is that units belong in field names. This format uses the
names above without unit suffixes (Decision D1). The unit of every
dimensioned column is instead carried in **field-level custom metadata**
under the key `unit` (values `mm`, `ns`, `MeV`, `MeV/c`), and in the table
above. Columns without a physical unit carry no `unit` key.

## 3. Schema metadata

The Arrow schema carries these custom metadata entries (key order as listed):

| Level | Key | Value |
|-------|-----|-------|
| schema | `vikshep.hits.schema_version` | `"1.0"` |
| field `x`, `y`, `z` | `unit` | `"mm"` |
| field `t` | `unit` | `"ns"` |
| field `edep` | `unit` | `"MeV"` |
| field `px`, `py`, `pz` | `unit` | `"MeV/c"` |

The IPC footer carries no custom metadata beyond the copy of the schema.

## 4. Ordering

Rows are ordered by `event_id` ascending. Within one event, rows are in
**detector step order**: the order in which Geant4 delivered the steps to the
sensitive detector while tracking that event.

The order MUST NOT depend on the number of worker threads. The writer therefore
uses a **reorder buffer**: each worker finishes its event into a private
buffer; events are released to the file writer strictly in `event_id` order,
and event `e` is released only after events `0` to `e-1` have been released.
Implementations MUST bound the memory of the buffer (for example by pausing
workers that are too far ahead). The bound has no effect on the bytes.

This relies on the random number stream of an event depending only on the run
seed and the `event_id`, not on which thread ran it. That is an open question
(see notes-B4.md). Until it is answered, thread-count independence of the
output is a requirement of this format, not a demonstrated property.

### 4.1 Event ids

`event_id` runs from 0 to `events - 1`, where `events` is the number of events
requested in the spec (Decision D4). Ids are consecutive in the run; the file
contains only the ids that have at least one hit.

## 5. Units, coordinates, empty events

### 5.1 Coordinate system

Right-handed Cartesian, in the Geant4 global (world) frame, with no further
transform. The beam travels along +z. The origin is the centre of the
detector as placed by the geometry (templates place their centre at the world
origin). For a geometry imported from GDML, the file's own world frame is used
unchanged.

### 5.2 Events with zero hits

An event with no hits has **no rows**. It is not represented in `hits.arrow`
at all. The number of events with and without hits is recorded in
`run_summary.json` (see SIDECAR.md). A run in which no event has a hit still
produces a valid `hits.arrow`: schema, dictionary, footer, and zero record
batches.

## 6. Fingerprint (OID)

- The OID of a run is the **SHA3-256 digest of the complete bytes of
  `hits.arrow`**, from the first magic byte to the last, taking the **first 14
  bytes** and writing them as **28 lowercase hexadecimal characters**.
- Nothing else is hashed: not `provenance.json`, not `run_summary.json`, not
  the optional geometry or trajectory files.
- The OID is stored as `oid` in `run_summary.json`, and is also the `oid` field
  of the `done` line in jsonl progress mode (SIDECAR.md). It is never written
  into `hits.arrow` itself.
- The OID rule is shared with other Vikshep projects and never changes.

## 7. Worked example

Illustrative values, not a test vector. A real golden file with its OID is a
Y3 deliverable.

Geometry has two sensitive volumes, so the dictionary is
`["gap_sensor", "tracker_strip"]` (indices 0 and 1). The run asked for 3
events. Event 1 had no hits, so it does not appear.

| event_id | volume (index) | copy_no | layer | x (mm) | y (mm) | z (mm) | t (ns) | edep (MeV) | px (MeV/c) | py (MeV/c) | pz (MeV/c) | track_id | parent_id | pdg | step |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | tracker_strip (1) | 12 | 2 | 1.250 | -0.730 | 20.125 | 0.0671 | 0.0123 | 0.84 | -0.12 | 3.51 | 1 | 0 | 11 | 4 |
| 0 | tracker_strip (1) | 13 | 2 | 1.310 | -0.700 | 20.300 | 0.0673 | 0.0071 | 0.83 | -0.12 | 3.50 | 1 | 0 | 11 | 5 |
| 2 | gap_sensor (0) | 0 | -1 | -4.020 | 2.115 | 45.600 | 0.1527 | 0.8800 | 0.10 | 0.02 | 1.90 | 3 | 1 | -11 | 1 |

With the 65536-row rule this file has one record batch of 3 rows.

## 8. Versioning

`vikshep.hits.schema_version` is `"<major>.<minor>"`.

- **Compatible (minor bump):** appending new columns after column 16. Readers
  MUST accept any minor version of a major they know and MUST ignore trailing
  columns they do not know.
- **Incompatible (major bump):** removing, renaming, reordering or retyping a
  column; changing a unit; changing the ordering, batch-size, dictionary or
  hit-definition rules; changing the OID computation (which MUST NOT change).
- v1 is frozen after Y3. After that, only minor bumps are allowed.
- A new minor or major version changes the bytes, hence the OID, of any run.

## 9. Determinism statement

The simulator is reproducible at **Tier 3** (the tier numbering of the Vikshep
compute project): the same build, on the same platform, with the same spec and
the same seed, produces a byte-identical `hits.arrow` and therefore the same
OID.

Identity of simulation output across operating systems, CPU architectures,
compilers or builds is **not promised and MUST NOT be claimed**. Reading and
analysing a stored `hits.arrow` is a deterministic function of its bytes and
gives the same result everywhere.

## Decisions to confirm

| ID | Decision | Proposed default | Alternative |
|----|----------|------------------|-------------|
| D1 | Column names vs the "units in field names" rule | Names exactly as specified (`x`, `edep`, ...), unit in field metadata `unit` | Rename to `x_mm`, `t_ns`, `edep_MeV`, `px_MeV_per_c`, ... |
| D2 | Rows per record batch | 65536, last batch partial | Another fixed size, or one batch per run |
| D3 | What is a hit | Step in a sensitive volume with edep > 0 | All steps in sensitive volumes, including edep = 0 |
| D4 | Event id range | 0 to events-1 | Spec-supplied first id |
| D5 | Buffer alignment | 8 bytes | 64 bytes (Arrow's recommendation); depends on writer, UNCLEAR until Y3 |
| D6 | Dictionary content | All sensitive volume names, sorted bytewise, written once | Names in order of first hit (depends on data), or volume as plain utf8 |
| D7 | Empty run | Valid file with zero record batches | One empty batch |
| D8 | Version string form | `"major.minor"` in schema metadata | Integer major only |
| D9 | Frame for imported GDML | File's own world frame, unchanged | Require a spec-supplied transform to the detector centre |
| D10 | Hit-free events | Absent from the file; counted in run_summary.json | A row with sentinel values |
