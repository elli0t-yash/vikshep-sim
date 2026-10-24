# vikshep-sim

Simulate pillar of Vikshep Studio: a Geant4-based simulator that turns a JSON
spec into hits.arrow, provenance.json and run_summary.json. Our only outputs are
files. Shared rule: an OID is the first 14 bytes of SHA3-256, written as 28
lowercase hex characters. Never change it.

Determinism is Tier 3: identical output on the same build, platform and seed.
Identity across platforms is not promised.

## Rules

- Geant4 inside, never instead of Geant4. No transport physics, no changes to
  Geant4 models, no Geant4 visualization drivers (we export glTF), no user
  interface in this repo.
- Never claim cross-platform identical simulation, speed we have not measured,
  or that Vikshep replaces Geant4.
- Branches y<N>/<topic>, one PR per milestone or smaller, CI green, never push
  to main. Stage files by explicit path.
- No emojis in code, docs or commit messages. No AI-attribution lines in files,
  commits or PRs. No secrets. No business or pricing content.
- Units are in field names. Nothing is unit-less.
- Interfaces I own: the simspec schema (versioned by spec_version), hits schema
  v1 (frozen after Y3), the sidecar contract SIDECAR.md (frozen after Y7). The
  OID rule is never changed. If an interface is unclear, open a GitHub issue
  titled "interface: <topic>" instead of guessing.
- Call templates "ATLAS-like" or "CMS-like", never "the ATLAS detector" or
  "the CMS detector". Only a collaboration's own exported geometry is the real
  thing.
- Never claim simulations are identical across operating systems. The promise
  is same machine, same build, same seed. Analysis of stored hits is identical
  everywhere.
- No secrets, business plans or prices in this repo.
- Keep STATUS.md current: milestone checklist, decisions, versions,
  measurements with dates.
- Geant4 credit lives in NOTICE.

## Checks

- scripts/check-rules.sh enforces the emoji, attribution and secret rules.
  Run it before every commit.
- Build: see README.md quick start.
