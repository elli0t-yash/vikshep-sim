# Simulation spec v1 (simspec)

Status: spec_version "1.0". The spec is an interface owned by the project and
is versioned by `spec_version`.

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
against its parent object (SIDECAR.md, section 6).

### 2.1 Top level

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| spec_version | string | yes | none | `"1.0"` | Lets the tool refuse a spec written for a different meaning. A well-formed but unsupported version gives `SPEC_VERSION_UNSUPPORTED`. |
| template | string | yes | none | `"layered_calorimeter"` | Selects the geometry builder. It is the only template for now. Templates are "ATLAS-like" or "CMS-like" in spirit, never the real detector. |
| geometry | object | yes | none | see 2.2 | Dimensions and materials of the template. |
| gun | object | yes | none | see 2.3 | The primary particle source. |
| physics | object | no | `{"list": "FTFP_BERT"}` | see 2.4 | Which Geant4 reference physics list to use. |
| sensitive | array of string | no | `["active"]` | non-empty, no repeats, each `"absorber"` or `"active"` | Which slabs record hits. Order is irrelevant. |
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
| position_mm | array of 3 numbers | no | `[0, 0, -20]` | any finite numbers | Start point `[x, y, z]` in mm, in the global frame of HITS.md. |
| direction | array of 3 numbers | no | `[0, 0, 1]` | finite, not all zero | Direction `[x, y, z]`. It is a direction, so it is unit-less. The tool normalises it to length 1. |

Note: the stack is centred on the origin, so the default start point
`[0, 0, -20]` is outside the stack only if its total length is below 40 mm.
Set `position_mm` explicitly to start upstream of a thicker stack
(Decision P1).

### 2.4 physics

| Field | Type | Required | Default | Valid values | Why it exists |
|-------|------|----------|---------|--------------|---------------|
| list | string | no | `"FTFP_BERT"` | a name accepted by G4PhysListFactory, as listed by `vikshep-sim info --json` (`physics_lists`), optionally followed by an EM option suffix from `em_options` | Selects the Geant4 reference physics list. Omitting the suffix selects the list's default EM. |

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
- Not checked in v1 and left to the run: whether the gun start point is inside
  the simulated world, and whether the Geant4 data sets needed by the physics
  list are installed (`DATASETS_MISSING`, exit 3).
- If `spec_version` is a string that is not supported, the result is a single
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
from the default start point `[0, 0, -20]` with the default physics list
`FTFP_BERT`; only the argon slabs record hits. 20 events are simulated with seed
42. With the defaults written out, the canonical form is:

```json
{"events":20,"geometry":{"absorber":{"material":"G4_Pb","thickness_mm":10},"active":{"material":"G4_lAr","thickness_mm":5},"layers":10,"transverse_mm":100},"gun":{"direction":[0,0,1],"energy_GeV":5,"particle":"e-","position_mm":[0,0,-20]},"physics":{"list":"FTFP_BERT"},"seed":42,"sensitive":["active"],"spec_version":"1.0","template":"layered_calorimeter"}
```

An invalid spec, for example `"thickness_mm": 0` in `absorber` and a misspelt
`"thickness"` in `active`, gives exit code 2 and, on stdout, one line:

```json
{"code":"SPEC_INVALID","errors":[{"message":"must be greater than 0","pointer":"/geometry/absorber/thickness_mm"},{"message":"missing required field 'thickness_mm'","pointer":"/geometry/active"},{"message":"unknown field; did you mean 'thickness_mm'?","pointer":"/geometry/active/thickness"}],"event":"error","message":"3 problems in spec"}
```

(The order of the entries follows section 3.)

## Decisions to confirm

| ID | Decision | Proposed default | Alternative |
|----|----------|------------------|-------------|
| P1 | Stack position vs default gun start | Stack centred on the origin (as HITS.md says); default start `[0,0,-20]` is then inside any stack longer than 40 mm | Put the front face of the stack at z = 0 (so the default is 20 mm upstream) and amend HITS.md section 5.1 |
| P2 | Required fields | `geometry`, `gun.particle`, `gun.energy_GeV`, `events` and `seed` are required; `physics`, `sensitive`, `gun.position_mm`, `gun.direction` are optional | Give `seed` a default |
| P3 | Layer order | Absorber then active within each layer, in +z | Active first |
| P4 | Volume names | `absorber` and `active` | Names with a prefix, for example `lc_absorber` |
| P5 | Missing field pointer | Points to the parent object (as SIDECAR.md) | Point to the missing field |
| P6 | Integer-valued numbers | `42.0` is accepted where an integer is required | Reject non-integer notation |
| P7 | Physics list EM suffix | Accepted when G4PhysListFactory accepts it | Plain list names only |
