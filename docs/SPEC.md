# Simulation spec v1 (simspec)

Status: spec_version "1.0" and "1.1". The spec is an interface owned by the
project and is versioned by `spec_version`. Version 1.1 adds the template
`tracker_barrel` and the optional `field` block (sections 2.5 to 2.7); it
changes nothing for a spec written as 1.0, which is accepted as before and
gives the same bytes, the same OID and the same `spec_oid` on the same build.

A spec is one JSON file that describes one simulation. Together with the build
of `vikshep-sim`, it fully determines `hits.arrow` (HITS.md). The machine
readable form of the rules below is `spec.schema.json` (JSON Schema 2020-12);
the reference implementation is the validator in `vikshep-sim`. Where a rule
cannot be written in JSON Schema (names that depend on the installed Geant4),
the validator is authoritative.

Words MUST and MAY are used in the usual sense.

## 1. General rules

- The file is a single JSON object, UTF-8.
- **Unknown fields are errors, at every level.** A misspelt field must not
  silently run a different experiment. The error message suggests the nearest
  known name.
- **Units are in field names** (`thickness_mm`, `energy_GeV`). Nothing is
  unit-less.
- **Threads are deliberately not in the spec.** The number of worker threads
  MUST NOT change the output, so it is not part of the description of the
  experiment. (It is a property of the run, reported in run_summary.json.)
- Numbers may be written as integers or decimals (`5` and `5.0` are the same
  value). A field described as an integer accepts any number with an integer
  value, as JSON Schema does.
- Duplicate keys within one object are not detected (the last one wins).

## 2. Fields

Required means the field MUST be present. A missing required field is reported
against its parent object (SIDECAR.md, section 6), and the report names the
field in an extra key `missing`.

### 2.1 Top level

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| spec_version | string | yes | none | `"1.0"` or `"1.1"` | Lets the tool refuse a spec written for a different meaning. A well-formed but unsupported version gives `SPEC_VERSION_UNSUPPORTED`. What 1.1 adds is in section 2.7. |
| template | string | yes | none | `"layered_calorimeter"`; `"tracker_barrel"` (1.1 only) | Selects the geometry builder. Templates are "ATLAS-like" or "CMS-like" in spirit, never the real detector. |
| geometry | object | yes | none | see 2.2 (`layered_calorimeter`) and 2.5 (`tracker_barrel`) | Dimensions and materials of the template. |
| gun | object | yes | none | see 2.3 | The primary particle source. |
| field | object | no | `{"B_T": [0, 0, 0]}` | see 2.6; `spec_version` 1.1 only | A uniform magnetic field. |
| physics | object | no | `{"list": "FTFP_BERT"}` | see 2.4 | Which Geant4 reference physics list to use. |
| sensitive | array of string | no | `["active"]` for `layered_calorimeter`, `["tracker_layer"]` for `tracker_barrel` | non-empty, no repeats; each `"absorber"` or `"active"` for `layered_calorimeter`, only `"tracker_layer"` for `tracker_barrel` | Which volumes record hits. Order is irrelevant. |
| events | integer | yes | none | 1 to 10000000 | Number of events to simulate. The upper bound keeps one run to a size the output format and the memory plan can handle. |
| seed | integer | yes | none | 0 to 2^53-1 (9007199254740991) | Random seed. Required so that every run states its seed; 2^53-1 is the largest integer that every JSON consumer reads exactly. |

### 2.2 geometry (template `layered_calorimeter`)

A stack of `layers` layers along +z. Each layer is one **absorber** slab
followed (in +z) by one **active** slab. All slabs are boxes with a square
transverse section of side `transverse_mm`, centred on the z axis. The stack is
centred on the origin, so its total length is
`layers * (absorber.thickness_mm + active.thickness_mm)` and it spans
`z = -length/2` to `z = +length/2` (HITS.md, section 5.1). Slabs are placed
with copy number = layer index (0-based), and the `layer` column of hits.arrow
is the same index. The physical volumes are named `absorber` and `active`.
Layer 0 is at the front (the most negative z).

**World.** The stack sits in a box of `G4_Galactic` centred on the origin. Its
side in x and y is `transverse_mm + 200` mm and its length in z is
`stack_length + 200` mm, so there are 100 mm of vacuum around the stack on every
side. The gun start point MUST lie strictly inside this box (a point on its
surface is outside). Example: for the worked example below the world is
300 mm by 300 mm by 350 mm, spanning |x|, |y| < 150 mm and |z| < 175 mm.

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| layers | integer | yes | none | 1 to 200 | Depth of the stack. |
| absorber | object | yes | none | `{material, thickness_mm}` | The dense passive slab that makes showers develop. |
| active | object | yes | none | `{material, thickness_mm}` | The slab that is read out. |
| transverse_mm | number | yes | none | greater than 0 | Side of the square cross-section, in mm. |

Each of `absorber` and `active`:

| Field | Type | Required | Valid values | Why it exists |
|-------|------|----------|--------------|---------------|
| material | string | yes | a Geant4 NIST material name, for example `G4_Pb`, `G4_lAr` | Names come from G4NistManager, so no material is defined by this project. |
| thickness_mm | number | yes | greater than 0 | Slab thickness along z, in mm. |

### 2.3 gun

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| particle | string | yes | none | one of `e-`, `e+`, `gamma`, `mu-`, `mu+`, `pi-`, `pi+`, `proton`, `neutron`, `kaon+`, `kaon-` | A fixed list of beam particles that the project tests. Geant4 has many more; they are not accepted in v1. |
| energy_GeV | number | yes | none | greater than 0 and at most 10000 | Kinetic energy of each primary, in GeV. |
| position_mm | array of 3 numbers | no | `layered_calorimeter`: `[0, 0, -(stack_length/2 + 10)]`; `tracker_barrel`: `[0, 0, 0]` | finite numbers, strictly inside the world (2.2 or 2.5) | Start point `[x, y, z]` in mm, in the global frame of HITS.md. For the calorimeter the default is on the z axis, 10 mm in front of the stack, so it follows the geometry. For the tracker it is the origin, the centre of the barrel. |
| direction | array of 3 numbers | no | `layered_calorimeter`: `[0, 0, 1]`; `tracker_barrel`: `[1, 0, 0]` | finite, not all zero | Direction `[x, y, z]`. It is a direction, so it is unit-less. The tool normalises it to length 1. The tracker default points along +x, perpendicular to the barrel axis, so that a particle from the origin crosses every layer; the calorimeter default is unchanged from 1.0. |

### 2.4 physics

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| list | string | no | `"FTFP_BERT"` | a name accepted by G4PhysListFactory, as listed by `vikshep-sim info --json` (`physics_lists`), optionally followed by an EM option suffix from `em_options` | Selects the Geant4 reference physics list. Omitting the suffix selects the list's default EM. |

### 2.5 geometry (template `tracker_barrel`, spec_version 1.1)

`N` concentric cylindrical layers (a "barrel") around the z axis, the beam
axis. Every layer is sensitive.

**Coordinate system.** The frame of HITS.md, section 5.1: right-handed, global
(world) frame, beam along +z. The barrel is centred on the origin: its axis is
the z axis and every layer spans `z = -half_length_mm` to `z = +half_length_mm`.
The transverse plane is x-y; a radius is `sqrt(x^2 + y^2)` in mm.

**Layer numbering.** `geometry.layers` is an array. Element 0 is the innermost
layer (smallest radius) and the index grows outwards. The index is the `layer`
column of hits.arrow and also the `copy_no` of the physical volume (HITS.md,
section 5.1).

**Shape of one layer.** A full hollow cylinder (a tube, 0 to 2 pi in azimuth)
with mid-surface radius `radius_mm`, wall thickness `thickness_mm`, so
inner radius `radius_mm - thickness_mm/2` and outer radius
`radius_mm + thickness_mm/2`, and half-length `half_length_mm` along z.

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| layers | array of object | yes | none | 1 to 100 elements, each as below | The layers, innermost first. |

Each element of `layers`:

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| radius_mm | number | yes | none | greater than 0, at most 10000 | Radius of the middle of the layer wall, in mm. |
| thickness_mm | number | yes | none | greater than 0 and less than `radius_mm` | Wall thickness in mm (radial). The upper limit keeps the inner radius at least `radius_mm / 2`. |
| half_length_mm | number | yes | none | greater than 0, at most 10000 | Half of the length along z, in mm. Layers may differ. |
| material | string | no | `"G4_Si"` | a Geant4 NIST material name | Silicon unless asked otherwise. Names come from G4NistManager, as in 2.2. |

**Ordering rules.** For every element `i` from 1 on, with
`outer(i) = radius_mm(i) + thickness_mm(i)/2` and
`inner(i) = radius_mm(i) - thickness_mm(i)/2`, computed in float64 without a
tolerance:

1. `radius_mm(i)` MUST be greater than `radius_mm(i-1)` (radii strictly
   increase);
2. `inner(i)` MUST be greater than `outer(i-1)` (layers neither overlap nor
   touch; there is a gap, filled with the vacuum of the world).

Both violations are reported against `/geometry/layers/<i>/radius_mm`, with a
message that names the layer it clashes with and the numbers. If rule 1 is
violated, rule 2 is not reported for the same element.

**World.** A box of `G4_Galactic` centred on the origin. Its half-extent in x
and y is `outer(N-1) + 100` mm (the outermost layer is the widest) and its
half-extent in z is `max(half_length_mm) + 100` mm, so there are 100 mm of
vacuum around the barrel on every side. The gun start MUST lie strictly inside
it. Example: for `docs/examples/tracker_muon.json` the outermost layer has
outer radius 300.15 mm and half-length 500 mm, so the world spans
|x|, |y| < 400.15 mm and |z| < 600 mm.

**Volume names.** The physical volume of every layer is named `tracker_layer`
(one name, copy number = layer index). The dictionary of hits.arrow (HITS.md,
section 1.2) is therefore `["tracker_layer"]`. A spec with the template
`tracker_barrel` has no `absorber` or `active` volume and no other field of
2.2: `transverse_mm` and the others are reported as unknown fields.

A hit is a Geant4 step with `edep > 0` in a layer, as everywhere (HITS.md,
section 2). A particle that crosses a layer therefore usually gives one hit per
step inside the wall. How many steps that is depends on Geant4 and on the
physics list; it is not specified.

### 2.6 field (spec_version 1.1)

An optional uniform magnetic field.

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| B_T | array of 3 numbers | no | `[0, 0, 0]` | finite numbers; the vector length is at most 10 | The field vector `[Bx, By, Bz]` in tesla (T). |

- **Units and frame.** Tesla. Components are along the x, y and z axes of the
  global frame of HITS.md, section 5.1. `B_T = [0, 0, 2]` is 2 T along +z, the
  beam direction.
- **Where it acts.** The same field everywhere in the world, vacuum and layers.
- **Zero means no field.** If all three components are zero (positive or
  negative zero), **no field object is created at all** and the run is the
  run of a spec without a `field` block: same transport, same random number
  use, same bytes. Only a non-zero vector creates a field.
- **Direction convention.** The force on a charge `q` with velocity `v` is
  `q v x B` (SI sign convention, Geant4's). For `B = [0, 0, Bz]` with `Bz > 0`
  a positive particle turns clockwise when seen from +z looking back towards
  the origin (that is, from a point on +z looking down onto the x-y plane with
  x to the right and y up), and a negative particle counter-clockwise. In the
  plane perpendicular to B the radius of curvature follows
  `pT[GeV] = 0.299792458 * |B|[T] * R[m]`, where `pT` is the momentum
  component perpendicular to B; the rule of thumb is `pT = 0.3 B R`.
  (The factor is c in units of 10^9 m/s, 0.299792458.)
- **Stepper.** For a non-zero field the tool installs a
  `G4UniformMagField` with `G4Mag_UsualEqRhs` and a `G4ExactHelixStepper`
  (the exact helix of a charged particle in a uniform field, so the integrator
  adds no error of its own; Geant4 still advances the track in chords and
  the energy loss and multiple scattering of the physics list act as usual)
  inside a `G4ChordFinder`. The Geant4 accuracy parameters (delta chord, delta
  one step, delta intersection) keep their Geant4 defaults. Changing the
  stepper would change the hits of every run with a field, so it is part of
  what this document defines.
- **Canonical form.** In a spec with `spec_version` 1.1 the field is always
  written out, defaults included, as `"field":{"B_T":[bx,by,bz]}`. A non-zero
  field is therefore part of the canonical spec and of `spec_oid`, and two
  specs that differ only in the field have different `spec_oid`s. (A zero
  field is written out too, as `[0,0,0]`; section 2.7 says why that does not
  make 1.0 and 1.1 specs equal.)

The `field` block applies to both templates.

### 2.7 What spec_version 1.1 changes

Every 1.1 feature is optional and its default equals the 1.0 behaviour.

| spec_version | Accepted |
|--------------|----------|
| `"1.0"` | exactly the rules of sections 2.1 to 2.4. `template` MUST be `layered_calorimeter` and there is no `field` block. |
| `"1.1"` | everything in 1.0, plus the template `tracker_barrel` (2.5) and the `field` block (2.6). |

- A spec written as 1.0 is validated by the 1.0 rules. A 1.1-only element in
  it is an error naming the version: template `tracker_barrel` is reported at
  `/template` and a `field` block at `/field`, with the message
  "requires spec_version 1.1".
- The canonical form keeps the `spec_version` of the file. A 1.0 spec has the
  same canonical form, hence the same `spec_oid`, as before 1.1 existed (the
  worked example of section 5 is unchanged byte for byte). The `field` block
  is written only for 1.1 specs.
- The same experiment written as 1.0 and as 1.1 has different `spec_oid`s,
  because the version string is part of the canonical bytes. With a zero
  field (or no `field`) the two give the same `hits.arrow`.
- `vikshep-sim info --json` lists both versions in `spec_versions`.

## 3. Validation

`vikshep-sim validate --spec <file>` checks everything above without running a
simulation and without initialising a Geant4 run.

- **All** problems are reported, not just the first. Each problem is a
  `{pointer, message}` pair; the pointer is an RFC 6901 JSON Pointer to the
  offending field (`/gun/energy_GeV`). The root object is the empty pointer.
  The messages say how to fix the problem.
- Order of reports: the fields are checked in the order of section 2; unknown
  fields are reported in key order after the known fields of the same object.
- Material, physics list and particle names are checked against what the
  installed Geant4 offers (materials, physics lists) and the fixed list above
  (particles). These checks need no Geant4 data sets.
- The gun start point is checked against the world of 2.2 or 2.5 (pointer
  `/gun/position_mm`). It is skipped while the geometry itself is invalid,
  because the world is then unknown. For `tracker_barrel` the world is known
  when every layer has a valid `radius_mm`, `thickness_mm` and
  `half_length_mm`; ordering errors (2.5) do not hide it.
- `template` is checked before `geometry`, and `geometry` is checked with the
  rules of its template; fields of the other template are unknown fields. If
  `template` is missing or not a valid name for this `spec_version`,
  `geometry` is checked as `layered_calorimeter`, as it always was in 1.0;
  fix `template` first, the geometry errors may then be noise.
  `field` is checked after `gun`, as in the table of 2.1.
- Layer elements are checked in index order; within one element in the order of
  the table in 2.5, then its ordering against the previous element.
- Not checked by `validate`: whether the Geant4 data sets needed by the physics
  list are installed. `run` and `dev-run` check that before starting
  (`DATASETS_MISSING`, exit 3).
- Only `"1.0"` and `"1.1"` are supported. If `spec_version` is a string that is not supported, the result is a single
  `SPEC_VERSION_UNSUPPORTED` error and nothing else is checked, because the
  rules of another version are unknown.
- A file that cannot be read gives `SPEC_UNREADABLE`; a file that is not JSON
  gives `SPEC_NOT_JSON`. All of these exit 2.

## 4. Canonical form

`vikshep-sim validate --spec <file> --canonical` prints the canonical form of a
valid spec. The same experiment always gives the same bytes, so the canonical
form can be hashed (the `spec_oid` in provenance.json, SIDECAR.md section 5.1).

- All defaults are written out. After this step every optional field is present.
- `gun.direction` is normalised to length 1 (each component divided by the
  Euclidean norm, computed in float64).
- `sensitive` is sorted in ascending order.
- For `tracker_barrel`, the order of `geometry.layers` is kept (the index is
  the layer number), every element has `material` written out, and the numbers
  of each element are float64 values in the shortest form. Defaults of `gun`
  are those of the template (2.3).
- For `spec_version` 1.1, `field` is written out with `B_T` (components as
  float64, `-0` written as `0`). For `spec_version` 1.0 there is no `field`.
- Object keys are sorted by their UTF-8 bytes; there is no whitespace.
- Numbers use the shortest representation that reads back to the same value,
  in the ECMAScript number format, and a number with an integer value is
  written as an integer (`5.0` becomes `5`, `-0` becomes `0`).
- Strings are written with the usual JSON escapes and no needless `\u` escapes.
- The output is followed by one newline (the newline is not part of the hashed
  bytes).

This follows RFC 8785 (JSON Canonicalization Scheme). The differences are
listed in the source (`spec_canonical.cc`): keys are sorted by UTF-8 bytes
rather than UTF-16 code units, which is the same for the ASCII keys that a
valid spec contains, and the tool only ever canonicalises specs it has
validated.

## 5. Worked example

`docs/examples/calorimeter.json`:

```json
{
  "spec_version": "1.0",
  "template": "layered_calorimeter",
  "geometry": {
    "layers": 10,
    "absorber": {"material": "G4_Pb", "thickness_mm": 10},
    "active": {"material": "G4_lAr", "thickness_mm": 5},
    "transverse_mm": 100
  },
  "gun": {"particle": "e-", "energy_GeV": 5},
  "events": 20,
  "seed": 42
}
```

Ten layers of 10 mm lead and 5 mm liquid argon give a 150 mm stack, 100 mm
across, spanning `z = -75` to `z = +75` mm. A 5 GeV electron is fired along +z
from the default start point `[0, 0, -85]` (10 mm in front of the stack, whose
front face is at `z = -75`) with the default physics list
`FTFP_BERT`; only the argon slabs record hits. 20 events are simulated with seed
42. With the defaults written out, the canonical form is:

```json
{"events":20,"geometry":{"absorber":{"material":"G4_Pb","thickness_mm":10},"active":{"material":"G4_lAr","thickness_mm":5},"layers":10,"transverse_mm":100},"gun":{"direction":[0,0,1],"energy_GeV":5,"particle":"e-","position_mm":[0,0,-85]},"physics":{"list":"FTFP_BERT"},"seed":42,"sensitive":["active"],"spec_version":"1.0","template":"layered_calorimeter"}
```

### 5.1 Worked example, tracker

`docs/examples/tracker_muon.json`:

```json
{
  "spec_version": "1.1",
  "template": "tracker_barrel",
  "geometry": {
    "layers": [
      {"radius_mm": 50,  "thickness_mm": 0.3, "half_length_mm": 500},
      {"radius_mm": 100, "thickness_mm": 0.3, "half_length_mm": 500},
      {"radius_mm": 150, "thickness_mm": 0.3, "half_length_mm": 500},
      {"radius_mm": 200, "thickness_mm": 0.3, "half_length_mm": 500},
      {"radius_mm": 250, "thickness_mm": 0.3, "half_length_mm": 500},
      {"radius_mm": 300, "thickness_mm": 0.3, "half_length_mm": 500}
    ]
  },
  "field": {"B_T": [0, 0, 2]},
  "gun": {"particle": "mu-", "energy_GeV": 1},
  "events": 50,
  "seed": 42
}
```

Six silicon layers of 0.3 mm wall at radii 50 to 300 mm, 1 m long, in a 2 T
field along +z. A 1 GeV (kinetic) mu- is fired from the origin along +x (the
tracker defaults) and curves in the x-y plane. The canonical form begins
`{"events":50,"field":{"B_T":[0,0,2]},"geometry":{"layers":[{"half_length_mm":500,"material":"G4_Si","radius_mm":50,"thickness_mm":0.3},...` and
ends `"sensitive":["tracker_layer"],"spec_version":"1.1","template":"tracker_barrel"}`;
the gun is `{"direction":[1,0,0],"energy_GeV":1,"particle":"mu-","position_mm":[0,0,0]}`.

An element error, for example the third layer at the same radius as the second
(`"radius_mm": 100`), gives exit code 2 and this entry in `errors`:

```json
{"message":"radius_mm must be greater than the radius of layer 1 (100 mm); layers are numbered from the innermost and radii must strictly increase","pointer":"/geometry/layers/2/radius_mm"}
```

### 5.2 Worked example, invalid calorimeter spec

An invalid spec, for example `"thickness_mm": 0` in `absorber` and a misspelt
`"thickness"` in `active`, gives exit code 2 and, on stdout, one line:

```json
{"code":"SPEC_INVALID","errors":[{"message":"must be greater than 0","pointer":"/geometry/absorber/thickness_mm"},{"message":"missing required field 'thickness_mm'","missing":"thickness_mm","pointer":"/geometry/active"},{"message":"unknown field; did you mean 'thickness_mm'?","pointer":"/geometry/active/thickness"}],"event":"error","message":"3 problems in spec"}
```

(The order of the entries follows section 3.)

## Decisions to confirm

| ID | Decision | Proposed default | Alternative |
|----|----------|------------------|-------------|
| P1 | Stack position vs default gun start | Decided: stack centred on the origin; default start is derived, `[0, 0, -(stack_length/2 + 10)]` | (was: fixed default `[0,0,-20]`) |
| P2 | Required fields | `geometry`, `gun.particle`, `gun.energy_GeV`, `events` and `seed` are required; `physics`, `sensitive`, `gun.position_mm`, `gun.direction` are optional | Give `seed` a default |
| P3 | Layer order | Absorber then active within each layer, in +z | Active first |
| P4 | Volume names | `absorber` and `active` | Names with a prefix, for example `lc_absorber` |
| P5 | Missing field pointer | Decided: parent object pointer plus a `missing` key naming the field | Point to the missing field |
| P6 | Integer-valued numbers | `42.0` is accepted where an integer is required | Reject non-integer notation |
| P7 | Physics list EM suffix | Accepted when G4PhysListFactory accepts it | Plain list names only |
| P8 | Version handling for 1.1 | One binary accepts 1.0 and 1.1; 1.0 specs keep their canonical form and `spec_oid`; 1.1-only elements in a 1.0 spec are errors | Reinterpret 1.0 specs as 1.1 (would change `spec_oid`) |
| P9 | Tracker volume name | One physical volume name `tracker_layer`, copy number = layer index | One name per layer (`tracker_layer_00`, ...) |
| P10 | `radius_mm` of a tracker layer | Radius of the middle of the wall | Inner radius |
| P11 | Touching layers | Rejected: a gap is required | Allow `inner(i) == outer(i-1)` |
| P12 | Tracker gun defaults | Position `[0,0,0]`, direction `[1,0,0]` | Direction `[0,0,1]` (never crosses a layer) |
| P13 | Field representation | `field.B_T`, a vector in tesla, whole world | Axial `Bz_T` only; or a field per volume |
