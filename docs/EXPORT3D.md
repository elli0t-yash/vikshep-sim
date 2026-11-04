# 3-D export v1 (geometry.glb and trajectories.json)

Status: proposed. Becomes a contract when merged.

This document defines two **optional** output files of `vikshep-sim run`, written
only when `run` is given `--export-3d` (SIDECAR.md, section 1.3). They exist so
that a viewer can show the detector and some of the particle tracks. They are
written to be moved, unedited, into another repository; they refer to sibling
documents only by file name (SPEC.md, HITS.md, SIDECAR.md).

Words MUST, SHOULD and MAY are used in the usual sense.

## 0. What the export is, and is not

- It is for **looking**. It is a **subset** of the simulation (section 4): the
  first events only, and only the more energetic tracks of them. Nothing in it
  is a substitute for `hits.arrow`, and an analysis MUST NOT use it.
- It is **not part of the OID**. The OID is the fingerprint of `hits.arrow`
  alone (HITS.md, section 6). Running with `--export-3d` does not change
  `hits.arrow`: the same spec and seed give the same bytes and the same OID with
  the flag on and off, for any number of threads. The two exported files have
  OIDs of their own (section 5), computed with the same rule, and those are
  recorded in `provenance.json` and `run_summary.json`, never in place of the
  OID of the hits.
- It is deterministic: the same build, platform, spec, seed and export flags
  give byte-identical files for any number of threads. (Same promise as
  HITS.md, section 9: nothing is promised across platforms, compilers or
  builds, and the files are not claimed to be identical across them. The
  geometry file contains float32 values computed with the C++ standard
  library's `cos` and `sin`.)
- Nothing in either file varies between runs: no timestamps, no host names, no
  paths, no tool version string.

## 1. Conventions

- **Frame and axes.** The Geant4 global frame of HITS.md, section 5.1:
  right-handed, origin at the centre of the detector, the beam along +z. **No
  axis is swapped or negated.** glTF viewers conventionally draw +y as "up";
  the file does not change the data to suit that, and says so in `asset.extras`
  (section 2.5). A viewer that wants the beam horizontal rotates the scene.
- **Length unit.** Geant4 and `hits.arrow` use millimetres. glTF 2.0 defines
  its unit as the metre. Decision: **`geometry.glb` is written in metres**
  (every position is the value in mm times 0.001), as the glTF specification
  requires, and `asset.extras.mm_per_unit` is `1000` so that a reader can
  recover millimetres exactly as `value * mm_per_unit`. **`trajectories.json`
  is written in millimetres**, the unit of the spec and of `hits.arrow`, and
  says so in its `unit` field. A viewer that overlays the two multiplies the
  trajectory points by 0.001.
- **Energy unit** in `trajectories.json`: GeV, as in the spec (`energy_GeV`).
  The unit is in the field name.
- **Charge** is in units of the elementary charge, an integer.

## 2. geometry.glb

A glTF 2.0 binary file (the `.glb` container of the glTF 2.0 specification:
a 12-byte header, one JSON chunk, one BIN chunk). Little endian. Hand-written,
no library; it uses only the core of glTF 2.0 and no extension.

### 2.1 Container

| Part | Content |
|------|---------|
| header | magic `0x46546C67` ("glTF"), version `2`, total length in bytes |
| chunk 0 | type `JSON` (`0x4E4F534A`), the JSON text, padded to a multiple of 4 bytes with spaces (0x20) |
| chunk 1 | type `BIN` (`0x004E4942`), the binary buffer, padded to a multiple of 4 bytes with zero bytes |

The JSON is compact (no whitespace) and its keys are sorted by their UTF-8
bytes. A number is written in the shortest decimal form that reads back to the
same float64 (SPEC.md, section 4); a value that is stored as float32 (the `min`
and `max` of the accessors) is first converted to float64, so it appears with
the digits of that double. There is exactly one buffer, the BIN chunk, with no
`uri`.

### 2.2 What is in it

**One node per physical volume of the detector, and nothing else.** The world
volume is not exported (it is vacuum and would hide everything). Nodes are
flat: the scene lists all of them as root nodes. Each node has one mesh. The
node count equals the number of physical volumes of the geometry (not counting
the world): `2 * layers` for `layered_calorimeter`, `N` for `tracker_barrel`
(SPEC.md, sections 2.2 and 2.5).

| Node field | Value |
|------------|-------|
| `name` | the name of the volume in the `volume` dictionary of `hits.arrow` (HITS.md, section 5.3): `absorber`, `active` or `tracker_layer`. Several nodes share a name; `extras.copy_no` tells them apart. |
| `mesh` | the mesh of the node (2.3) |
| `translation` | the centre of the volume in metres, `[x, y, z]`. Absent when it is the origin (every tracker layer). |
| `extras` | `{"copy_no": <int>, "role": "absorber" \| "active" \| "tracker", "sensitive": <bool>}` |

- `copy_no` is the copy number of the physical volume, which is the layer index
  (SPEC.md); it is the same number as the `copy_no` column of `hits.arrow`.
- `role` is `absorber` or `active` for the two slabs of the calorimeter and
  `tracker` for the layers of the tracker.
- `sensitive` is true if the volume's name is in the spec's `sensitive` list
  (SPEC.md, section 2.1), that is, if the volume can produce hits.

**Ordering of nodes (stable).** Node `i` is, for `layered_calorimeter`: layer
`i / 2`, absorber if `i` is even, active if `i` is odd (the order of the
placement in +z, SPEC.md 2.2). For `tracker_barrel`: layer `i`, innermost
first. The order of meshes, materials, accessors, buffer views and the layout of
the BIN chunk follow from it (2.3 and 2.4), so the file is a pure function of
the spec.

### 2.3 Meshes

Every mesh has one primitive with mode 4 (triangles) and the attributes
`POSITION` and `NORMAL` (both float32 `VEC3`) and `indices` (unsigned 16-bit
scalar), and a material (2.4). Triangles are wound counter-clockwise seen from
outside, the glTF front face. Normals are unit vectors.

- **layered_calorimeter: boxes.** Meshes `absorber` and `active` (in that
  order), shared by all nodes of their slab type. Each is a box centred on its
  own origin with the slab's dimensions: `transverse_mm` by `transverse_mm` in x
  and y, the slab thickness in z. 24 vertices (4 per face, flat normals), 36
  indices, faces in the order +x, -x, +y, -y, +z, -z.
- **tracker_barrel: tessellated tubes.** One mesh per layer, named
  `tracker_layer_<i>` (`i` is the layer index), because the layers differ in
  radius, thickness and length. A tube is the hollow cylinder of SPEC.md 2.5,
  tessellated with **64 segments** around the z axis (`SEGMENTS = 64`): vertex
  `k` of a ring is at azimuth `2*pi*k/64`, `k = 0..63`, counter-clockwise seen
  from +z, `k = 0` on +x, the radius being that of the ring (the true radius,
  not the radius of an inscribed polygon's mid-edge). It has four parts, in
  this order, each with its own vertices so that normals are right:
  1. outer wall: 2 rings of 64 vertices (z = -half_length, then +half_length) at
     the outer radius, normals radial outwards;
  2. inner wall: 2 rings at the inner radius, normals radial inwards, wound so
     that it faces the bore;
  3. end cap at +z: 2 rings (outer, inner) at z = +half_length, normal +z;
  4. end cap at -z: 2 rings (outer, inner) at z = -half_length, normal -z.
  512 vertices and 1536 indices per tube. The real tube is round; the mesh is
  a 64-gon, whose vertices lie on the true surface and whose edges lie inside it
  by at most `r * (1 - cos(pi/64))`, about 0.12 % of the radius.
- Vertex values are computed in float64 in millimetres, multiplied by 0.001
  and then converted to float32.

### 2.4 Materials, accessors, buffer

- Materials are cosmetic and fixed: `absorber` base colour
  `[0.6, 0.6, 0.65, 0.35]`, `active` `[0.2, 0.5, 0.8, 0.35]`, `tracker`
  `[0.2, 0.7, 0.4, 0.35]`; `pbrMetallicRoughness` with `metallicFactor 0`,
  `roughnessFactor 0.9`, `alphaMode "BLEND"`, `doubleSided true`. A viewer MAY
  replace them.
- BIN layout: for each mesh in order, its `POSITION` data, its `NORMAL` data,
  its indices, each starting on a 4-byte boundary (indices are padded with
  zero bytes). One buffer view per accessor (target 34962 for vertex data, 34963
  for indices, no `byteStride`), one accessor per buffer view, in the same
  order. `POSITION` accessors carry `min` and `max` (required by glTF), in
  metres, as the float32 values written.

### 2.5 Asset and extras

`asset` is `{"generator": "vikshep-sim", "version": "2.0", "extras": {...}}`.
`asset.extras` has exactly these keys:

| Key | Value |
|-----|-------|
| `export_format_version` | `"1.0"` |
| `length_unit` | `"m"` (the unit of every position in this file) |
| `mm_per_unit` | `1000` |
| `frame` | `"geant4_global"` |
| `beam_axis` | `"+z"` |
| `axes` | `"x, y, z of the Geant4 global frame; no axis swapped (glTF's conventional up is +y; the file does not remap it)"` |
| `template` | `"layered_calorimeter"` or `"tracker_barrel"` |
| `spec_oid` | the `spec_oid` of the run (SIDECAR.md, section 5.1) |
| `tube_segments` | `64` for `tracker_barrel`; absent for the calorimeter |

There is one scene, `{"nodes": [0 .. n-1]}`, and it is the default scene.

## 3. trajectories.json

A UTF-8 JSON file, one top-level object, then the events. It is written in
this fixed layout so that the bytes are a pure function of the data:

```
{"format_version":"1.0","unit":"mm","spec_oid":"<28 hex>","min_energy_GeV":0.001000,
"max_tracks_per_event":500,"events_requested":10,"events":[
<event>,
<event>
]}
```

(shown on several lines for reading; the file has a newline after each of the
lines `...,"events":[` and after every event object, and one final newline
after `]}`). Keys appear in the order shown and in the order of the tables
below, not sorted.

### 3.1 Top level

| Key | Type | Meaning |
|-----|------|---------|
| format_version | string | `"1.0"` |
| unit | string | `"mm"`: the unit of every coordinate in `points` |
| spec_oid | string | the `spec_oid` of the run (SIDECAR.md, section 5.1), so that the file can be tied to its run |
| min_energy_GeV | number | the `--export-min-energy` in effect, in GeV, written as in 3.4 |
| max_tracks_per_event | integer | the cap of section 4 (a constant of the format: 500) |
| events_requested | integer | the `--export-events` in effect, before limiting it to the number of events of the run |
| events | array | the exported events, in ascending `event_id`; `min(events_requested, events of the spec)` entries |

### 3.2 Event

| Key | Type | Meaning |
|-----|------|---------|
| event_id | integer | the event id of `hits.arrow` (HITS.md, section 4.1). The exported events are ids `0` to `n-1`. |
| tracks_total | integer | the number of tracks Geant4 created in this event (primaries and secondaries) |
| dropped_tracks | integer | `tracks_total` minus the number of entries of `tracks`: the tracks that are not in the file (section 4). A viewer SHOULD tell the user when this is not 0. |
| tracks | array | the kept tracks, in ascending `track_id` |

An event with no hits is exported like any other (it has at least its
primary track). `tracks_total` counts all tracks, whether or not they touched a
sensitive volume.

### 3.3 Track

| Key | Type | Meaning |
|-----|------|---------|
| track_id | integer | Geant4 track id, 1-based within the event: the `track_id` column of `hits.arrow` |
| parent_id | integer | track id of the parent, 0 for a primary (as in `hits.arrow`) |
| pdg | integer | PDG Monte Carlo particle code (as `hits.arrow`) |
| charge | integer | charge in units of the elementary charge |
| kinetic_energy_start_GeV | number | kinetic energy when the track was created, in GeV |
| points | array of `[x, y, z]` | the polyline of the track, in mm, in the global frame (section 1). The first point is where the track was created; then, for every step of the track in order, the position at the end of the step (the post-step point). A track with no step has one point. Steps are not thinned. In a magnetic field the path between two points is an arc that the polyline approximates by its chords. |

### 3.4 Number formatting (deterministic)

Numbers are written with C `printf`-style fixed precision, never in
exponent form, never with a plus sign or leading zeros, no thousands
separators:

- coordinates (`points`): `%.3f`, millimetres, that is, a resolution of 1
  micrometre; the value is the nearest double to the decimal written (the
  rounding is that of the C library's `printf` for the double that Geant4
  gave); `-0.000` is written as `0.000`;
- `kinetic_energy_start_GeV` and `min_energy_GeV`: `%.6f`, a resolution of 1
  keV, with trailing zeros kept (`1.000000`);
- integers: decimal, no padding. NaN and infinity MUST NOT appear; the
  simulator treats one as an export failure.

Points are written as `[x,y,z]` with no spaces.

## 4. What is kept: the export is a subset

Three limits apply, and the file records the ones that are not constants of
the event itself. All three are applied in a way that depends only on the
simulated data and the limits, never on the number of threads or on which
thread finished first.

1. **Events.** The first `N` events by `event_id`: ids `0` to `N-1`, where `N`
   is `--export-events` (default **10**, an integer from 1 to a hard cap of
   **100**), limited to the number of events of the run. The events are chosen by
   id, not by the order in which worker threads finish.
2. **Energy.** A track is kept only if it is a **primary** (`parent_id` 0), or if
   its `kinetic_energy_start_GeV` is at least `--export-min-energy` (default
   **0.001** GeV, that is 1 MeV; a number from 0 to 10000 in GeV; with 0 every
   track is eligible). The primary is kept whatever its energy.
3. **Count.** At most **500** tracks are kept per event (a constant of the
   format). If more tracks are eligible, all primaries are kept, and then the
   others in descending `kinetic_energy_start_GeV`, ties broken by ascending
   `track_id`, until 500 are kept. The kept tracks are then written in ascending
   `track_id`.

`dropped_tracks` (3.2) is the count of the tracks removed by 2 and 3 together.
A track's hits in `hits.arrow` are unaffected by whether the track was kept.

`geometry.glb` is not a subset: it always contains every volume.

## 5. Files, OIDs and the output folder

- Both files are written into the same temporary folder as the other outputs
  and move into place with it, atomically (SIDECAR.md, section 5.3). A failure
  of the export writes nothing: the temporary folder is removed and the run
  ends with the error `INTERNAL` (SIDECAR.md, section 4), never with a
  partial output folder.
- The OID of an exported file is the first 14 bytes of the SHA3-256 digest of
  its complete bytes, as 28 lowercase hex characters (HITS.md, section 6). It is
  recorded in `provenance.json` and `run_summary.json` as described in
  SIDECAR.md, section 5, next to the file's size in bytes. Two runs with the
  same build, platform, spec, seed and flags have the same export OIDs.
- Without `--export-3d` neither file exists and nothing about them appears in
  `provenance.json`, `run_summary.json` or the `done` line: those are
  byte-identical to what they were before the export existed.

## 6. Versioning

`format_version` of `trajectories.json` and `asset.extras.export_format_version`
of `geometry.glb` are `"<major>.<minor>"`, `"1.0"` for this document. Adding
optional keys is a minor change; readers MUST ignore keys they do not know.
Changing the meaning or unit of a key, the tessellation, the node order or the
selection rules of section 4 is a major change.

## Decisions to confirm

| ID | Decision | Proposed default | Alternative |
|----|----------|------------------|-------------|
| X1 | Length unit of the glb | Metres (glTF's unit), `mm_per_unit` 1000 in the extras | Millimetres with a scale in the extras |
| X2 | Axes | Geant4 frame unchanged, beam +z recorded in the extras | Rotate to glTF's y-up |
| X3 | World volume | Not exported | Exported as a transparent box |
| X4 | Tube segments | 64 | Another fixed number, or chosen from the radius |
| X5 | Calorimeter meshes | One shared mesh per slab type | One mesh per node |
| X6 | Event and track limits | 10 events (cap 100), 1 MeV, 500 tracks per event | Other values |
| X7 | trajectories.json unit | mm, energy in GeV | metres |
| X8 | Export OIDs | Own OIDs in provenance.json and run_summary.json, never the OID of the run | Fold into the run OID (rejected: the OID rule is fixed) |
