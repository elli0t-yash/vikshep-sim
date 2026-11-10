# 3-D export (geometry.glb and trajectories.json, trajectories format 1 and 2)

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

- Format 2 of `trajectories.json` (section 7) adds time, energy, deposited energy and
  process names, so that a viewer can **replay an event that has already been simulated**,
  in physical time, and mark where each interaction happened. It is a replay of stored
  data, like the event display Geant4 offers after an event. It is **not** a live run:
  nothing is simulated while the viewer plays, and the viewer MUST NOT claim otherwise.

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
and `max` of the accessors, the `translation` of a node) is written in the
shortest decimal form that reads back to the same float32. There is exactly one buffer, the BIN chunk, with no
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

This section defines **format 1** (`format_version` `"1.0"`). Format 2 (`"2.0"`) is
defined in section 7 and is what `vikshep-sim` writes since the Y12 milestone; it contains
everything of format 1 under the same names, plus the additions of section 7. Files of
format 1 stay valid, and a viewer MUST read both (section 6).

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

The rule is applied while the event is being tracked, not after it: the
simulator never holds more than 500 tracks of an event. A track's rank (primary,
then energy, then `track_id`) is known when it starts, so a track that cannot be
among the 500 is not recorded at all, and a recorded one that is pushed out by a
better-ranked one is freed. The file is exactly the one the rule above gives
for all tracks of the event; only the memory is bounded. What this does not bound
is the size of Geant4's own track stack, which the export does not touch.

`dropped_tracks` (3.2) is the count of the tracks removed by 2 and 3 together.
A track's hits in `hits.arrow` are unaffected by whether the track was kept.

`geometry.glb` is not a subset: it always contains every volume.

## 5. Files, OIDs and the output folder

- Both files are written into the same temporary folder as the other outputs
  and move into place with it, atomically (SIDECAR.md, section 5.3). A failure
  of the export writes nothing: the temporary folder is removed and the run
  ends with the error `INTERNAL` (SIDECAR.md, section 4), never with a
  partial output folder.
- Test hook, not a feature: if the environment variable
  `VIKSHEP_TEST_EXPORT_FAIL` is `1`, the export fails on purpose, so that the
  test suite can check the paragraph above. It exists only for tests, it is not
  part of any contract (SIDECAR.md does not list it), it may be removed or
  renamed without a version change, and no user or tool should set it.
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
of `geometry.glb` are `"<major>.<minor>"` and are versioned **separately**:
`geometry.glb` stays `"1.0"`; `trajectories.json` is `"2.0"` (section 7) and was `"1.0"`
(section 3). Adding optional keys is a minor change; readers MUST ignore keys they do not
know. Changing the meaning or unit of a key, the tessellation, the node order or the
selection rules of section 4 is a major change.

Why format 2 is `"2.0"` and not `"1.1"`: every point of a track changes from
`[x,y,z]` to a longer array (section 7.3). A format 1 reader that took points as
`[x,y,z]` would still read the first three numbers, but that is luck, not a promise, so the
change is declared major. The rule for readers:

- A reader MUST accept `format_version` `"1.0"` and `"2.0"` (and ignore unknown keys of any
  minor version of them). It MUST read the first three numbers of a point as x, y, z in
  both.
- A viewer of a format 1 file has no time, energy, process or interaction data. It SHOULD
  still show the tracks, disable what needs the missing data, and say so.
- The flag stays `--export-3d` (SIDECAR.md, section 1.3). There is no option to write
  format 1; the app asks for the export and gets the newest format. The `format_version`
  in `provenance.json` (`export_3d`) is that of `trajectories.json`.

## 7. Format 2 of trajectories.json (physics-rich)

`format_version` `"2.0"`. Additive in content, major in version (section 6). Everything of
sections 3.1 to 3.3 and 4 holds unless this section says otherwise. Keys still appear in
the order of the tables, not sorted.

### 7.1 Top level

As in 3.1, with these changes (the header line of the file, with its newline, now reads
`{"format_version":"2.0","unit":"mm","spec_oid":"...","min_energy_GeV":...,"max_tracks_per_event":500,"max_interactions_per_event":2000,"events_requested":N,"point_fields":[...],"events":[`):

| Key | Type | Meaning |
|-----|------|---------|
| format_version | string | `"2.0"` |
| max_interactions_per_event | integer | the cap of 7.5 (a constant of the format: 2000). New. |
| point_fields | array of 7 strings | the meaning and unit of each element of a point, in order: `["x_mm","y_mm","z_mm","t_ns","kinetic_energy_GeV","edep_GeV","process"]`. New. Constant in this version; it is there so that the file explains itself. |

### 7.2 Event

As 3.2, plus:

| Key | Type | Meaning |
|-----|------|---------|
| interactions_total | integer | the number of interactions (7.4) Geant4 produced in this event, whether or not their track is in the file. New. |
| dropped_interactions | integer | `interactions_total` minus the number of entries of `interactions`. A viewer SHOULD tell the user when this is not 0. New. |
| tracks | array | as 3.2 |
| interactions | array | the kept interactions (7.4), ordered by ascending `t_ns`, ties by ascending `track_id`, then ascending `point_index`. New. |

### 7.3 Track and point

A track has these keys, in this order. `track_id`, `parent_id`, `pdg`, `charge` and
`kinetic_energy_start_GeV` are as in 3.3. New: `particle`, `creator_process`, `end_process`.

| Key | Type | Meaning |
|-----|------|---------|
| track_id | integer | as 3.3 |
| parent_id | integer | as 3.3 |
| pdg | integer | as 3.3 |
| particle | string | the Geant4 particle name (`gamma`, `e-`, `e+`, `mu-`, `proton`, ...). New. |
| charge | integer | as 3.3 |
| creator_process | string | the name of the Geant4 process that created the track (`compt`, `conv`, `eIoni`, ...), or `"primary"` for a primary (`parent_id` 0). New. |
| end_process | string | the name of the process that ended the track's last step, as in the `process` of its last point. `"Transportation"` means the track left the world. New. |
| kinetic_energy_start_GeV | number | as 3.3 |
| points | array of points | the polyline, as 3.3, now with seven elements per point |

A **point** is an array `[x_mm, y_mm, z_mm, t_ns, kinetic_energy_GeV, edep_GeV, process]`:

| Element | Type | Unit | Meaning |
|---------|------|------|---------|
| x_mm, y_mm, z_mm | number | mm | position, as 3.3 |
| t_ns | number | ns | global time at that point: the time since the primary was generated at t = 0 in this event (Geant4 global time) |
| kinetic_energy_GeV | number | GeV | kinetic energy of the track at that point (after the step) |
| edep_GeV | number | GeV | energy deposited in the **step that ended at this point**, in any volume, sensitive or not (Geant4's total energy deposit of the step, `GetTotalEnergyDeposit`). 0 for the first point |
| process | string or null | | the name of the Geant4 process that ended the step (the process that defined the post-step point). `null` for the first point, which ends no step |

Consequences a reader can rely on:

- `t_ns` never decreases along a track. For a particle that travels in a straight line the
  time difference between two points is the path length divided by its speed; for a photon
  (or any particle moving at c in vacuum, and a photon in matter, which Geant4 does not
  slow down) it is the length divided by c = 299.792458 mm/ns.
- The energy of a track at its end is the `kinetic_energy_GeV` of its last point.
  For a track with `end_process` `"Transportation"` this is the energy that left the
  world with it.
- Where the energy of a step was deposited. The step lies inside one volume, and its
  post-step point usually lies on the volume's surface, so the post-step point does not say
  which volume. A viewer MUST use the **midpoint of the segment** between the previous point
  and this point (in a field the chord, which for these steps lies inside the thin
  volume) to find the volume. This is exact for the straight steps of the calorimeter and
  a good approximation for the short steps of the tracker.
- Number format (additive to 3.4): `t_ns` is written `%.6f` (1 femtosecond; light travels
  0.3 micrometres in it), `kinetic_energy_GeV` and `edep_GeV` are written `%.7f` (0.1 keV).
  A step that deposits less than 0.05 keV is therefore written as `0.0000000`. The energy
  checks of section 7.7 depend on this resolution. The string is written as JSON, with
  the escapes of RFC 8259; Geant4's process names are plain ASCII.
- Points are written `[x,y,z,t,e,d,"process"]` without spaces; `null` is written as
  `null`.

### 7.4 Interactions

An **interaction** is a step that ended in a process the user would call an event in the
life of the particle, as opposed to the particle just travelling. A step is an interaction
if its ending process is not one of `Transportation`, `CoupledTransportation`, `msc`,
`StepLimiter`, `UserSpecialCuts` (travelling, multiple scattering and artificial step
limits), and, if its name ends in `Ioni` (continuous-style ionisation), only if the step also
created at least one secondary (a delta ray). A track that simply slows to rest by
ionisation is therefore not an interaction; a track absorbed by `phot`, annihilated by
`annihil` or decayed by `Decay` is. Rule applied to Geant4's own names, not mapped.

Only interactions of the tracks that are in the file (4.1, 4.2, 4.3) are listed, so that
every interaction can be drawn on a track. `interactions_total` counts the interactions
of all tracks, so the difference is visible.

| Key | Type | Meaning |
|-----|------|---------|
| track_id | integer | the track whose step it is (a track of `tracks`) |
| point_index | integer | index in that track's `points` of the point that ends the step; `points[point_index]` has the same position and time |
| t_ns | number | global time, ns, `%.6f`; equals the `t_ns` of that point |
| position_mm | array `[x,y,z]` | position, mm, `%.3f`; equals that point's |
| process | string | the ending process, as in the point |
| secondaries | array of integers | the `track_id` of every secondary created in the step, in ascending order; empty if none. A listed id may belong to a track that is not in `tracks` (dropped by 4); `tracks_total` counts it |
| edep_GeV | number | energy deposited in the step, GeV, `%.7f`; equals that point's |

### 7.5 Selection rules, caps and dropped counts

All the rules of section 4 apply to tracks unchanged (500 tracks per event, `dropped_tracks`).
For interactions:

1. Only the interactions of kept tracks are candidates (7.4).
2. **Cap: at most 2000 interactions per event** (a constant of the format,
   `max_interactions_per_event`). If more are candidates, those with the largest
   **transferred energy** are kept, where transferred energy is the step's `edep_GeV` plus
   the sum of the kinetic energies at creation of its secondaries; ties are broken by
   ascending `t_ns`, then ascending `track_id`, then ascending `point_index`. The kept
   interactions are then written in the order of 7.2. The rule depends only on the simulated
   data, never on the number of threads.
3. `dropped_interactions` = `interactions_total` - the number kept.

Only the 500-track memory bound of section 4 holds while the event is tracked; the
interactions of stored tracks are kept with them and freed with them, and the final
choice of 2. is made when the event ends.

### 7.6 Size

Format 2 adds four numbers and one string to every point and the interaction list, so
the file is larger. Measured on this build (macOS arm64, Release, 4 threads,
`docs/examples/electron_shower.json`, 5 GeV electrons, the default 10 events, 1 MeV, 500
tracks):

Measurements are added in a later commit of this milestone, once the writer exists.

The cap on tracks bounds the file: at most 100 events x 500 tracks, each with its
points. If a file is too large for a given use, the proposal for a compact encoding is in
7.8; it is not part of format 2.

### 7.7 The export is a read-only view; what can be checked from it

- The export only **reads** what Geant4 already computes, in the stepping and tracking
  hooks. It changes no physics, no seeding and no random number. Therefore
  `hits.arrow`, and its OID, with `--export-3d` equal those without it, for 1 and for 4 threads
  (a test compares them, section 5), and the same spec and seed give identical files for
  any number of threads.
- On an event where nothing was dropped (`dropped_tracks` is 0 and `dropped_interactions`
  is 0, which needs `--export-min-energy 0` and fewer than 500 tracks), the energy balance
  holds: the sum of `edep_GeV` over all points of all tracks, plus the `kinetic_energy_GeV`
  of the last point of every track whose `end_process` is `"Transportation"` (the
  energy that left the world), equals the kinetic energy of the primary
  (`kinetic_energy_start_GeV` of track 1) within the tolerance the tests state (tests/check_export3d.py; the measured values are in STATUS.md). The
  balance is not exact: nuclear binding and neutrino energy are not a deposit, and
  rounding to 0.1 keV per number adds up over the steps.
- Dropped tracks break the balance by their own energy; a viewer that shows "energy
  still in flight" for an event with dropped tracks shows a lower bound.

### 7.8 Proposal (not part of format 2): a compact encoding

A point as a row repeats the same short strings (`Transportation`, `eIoni`, ...) and the
brackets for every point. The alternative is **columns per track**: instead of `points`,
`"point_columns":{"x_mm":[..],"y_mm":[..],"z_mm":[..],"t_ns":[..],"kinetic_energy_GeV":[..],"edep_GeV":[..],"process":[..]}`,
with `process` as indices into a top-level `"processes":[...]` list in order of first use.
It is smaller because the process names and the delimiters are not repeated, and because
a column of similar numbers compresses better; it is faster to load into typed arrays.
The measured saving, from the same file, is in 7.6. It would be a new major version, because
it removes `points`. It is proposed only if the size above is a problem; with the
measured sizes it is not recommended now.

## 8. Teaching examples

Two specs in `docs/examples/` are meant for looking at, not for physics analysis. Both are
run with the export and **all tracks and interactions kept**:

```sh
vikshep-sim run --spec docs/examples/<name>.json --out <dir> --export-3d --export-min-energy 0
```

The two examples are added in a later commit of this milestone, with their measured numbers.

## 9. Replay in scripts/view_3d.html

The viewer in `scripts/view_3d.html` plays format 2 in physical time: a time slider with play,
pause, step and speed; tracks grow with time with a bright head; interaction markers appear at
their time, coloured by process; slabs glow with the energy deposited so far; a readout shows
time, tracks alive, deposited energy and energy in flight. It is a **replay of the stored
event**, not a live simulation. A format 1 file loads with the playback disabled.

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
| X9 | Format 2 version number | `"2.0"` (points change shape); `geometry.glb` stays `"1.0"` | `"1.1"` |
| X10 | Interaction definition | Steps ended by a process other than travelling, scattering or step limits; ionisation only with a delta ray (7.4) | Every step with a discrete process |
| X11 | Interaction cap | 2000 per event, by transferred energy (7.5) | Earliest in time |
| X12 | Where energy was deposited | Midpoint of the step segment (7.3) | An extra volume field on every point |
