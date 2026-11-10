// The logic of the replay in scripts/view_3d.html (the script with id "replay-core"), run in Node on the
// committed fixtures, and static checks of the page. The browser part (three.js, the DOM) is not run here.
// Usage: node check_viewer_core.mjs <view_3d.html> <fixtures dir>
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';

const [htmlPath, fixtures] = process.argv.slice(2);
const html = fs.readFileSync(htmlPath, 'utf8');
let failed = 0;
const expect = (ok, text) => { console.log((ok ? 'PASS  ' : 'FAIL  ') + text); if (!ok) failed++; };

// ---- static checks of the page
const core = /<script id="replay-core">([\s\S]*?)<\/script>/.exec(html);
expect(core !== null, 'the page has a replay-core script');
const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
const used = new Set([...html.matchAll(/\$\('([^']+)'\)/g)].map((m) => m[1]));
const missing = [...used].filter((i) => !ids.has(i));
expect(missing.length === 0, 'every $(id) used by the page exists in the page' + (missing.length ? ': missing ' + missing.join(', ') : ' (' + used.size + ' ids)'));
const importmap = JSON.parse(/<script type="importmap">([\s\S]*?)<\/script>/.exec(html)[1]);
const hashes = Object.entries(importmap.integrity);
expect(hashes.length === 7 && hashes.every(([url, h]) => /^https:\/\/cdn\.jsdelivr\.net\/npm\/three@0\.160\.0\//.test(url) && /^sha384-[A-Za-z0-9+/]{64}$/.test(h)),
  'three.js stays pinned to 0.160.0 with seven sha384 integrity hashes');
const preloads = [...html.matchAll(/<link rel="modulepreload"[^>]*integrity="(sha384-[^"]+)"/g)].map((m) => m[1]);
expect(preloads.length === 7 && preloads.every((h) => hashes.some(([, x]) => x === h)), 'the seven modulepreload links carry the same hashes');
const external = [...html.matchAll(/(?:src|href)="(https?:[^"]+)"/g)].map((m) => m[1]).filter((u) => !u.includes('cdn.jsdelivr.net/npm/three@0.160.0/'));
expect(external.length === 0, 'no other external resource is loaded');

// ---- the core
const context = vm.createContext({});
vm.runInContext(core[1], context);
const R = context.ReplayCore;
const load = (name) => JSON.parse(fs.readFileSync(path.join(fixtures, name), 'utf8'));
const v1 = R.normalise(load('trajectories_format1_v0.1.0_tracker_muon.json'));
const v2 = R.normalise(load('trajectories_format2_teaching_muon_field.json'));
expect(v1.version === '1.0' && !v1.rich && v1.events.length === 10 && v1.events[0].tracks[0].t === null && v1.events[0].tracks[0].n >= 7,
  'format 1 (release v0.1.0) loads: 10 events, x/y/z, no time');
expect(v2.version === '2.0' && v2.rich && v2.events.length === 10 && v2.events[0].tracks[0].t[0] === 0, 'format 2 loads: time starts at 0');
let refused = false;
try { R.normalise({ format_version: '3.0', events: [] }); } catch (e) { refused = /3\.0/.test(e.message); }
expect(refused, 'an unknown format_version is refused with a message');
expect(R.normalise({ ...load('trajectories_format2_teaching_muon_field.json'), future_key: 1 }).rich, 'unknown keys are ignored');

// layers of tests/fixtures/trajectories_format2_teaching_muon_field.json (docs/examples/teaching_muon_field.json)
const volumes = [50, 100, 150, 200, 250, 300].map((r) => ({ kind: 'tube', rin: r - 1, rout: r + 1, zhalf: 500 }));
const TOL = 5e-6;   // GeV, as in tests/check_export_physics.py
let worst = 0, checked = 0, monotone = true;
for (const ev of v2.events) {
  if (ev.dropped_tracks !== 0) continue;
  R.assignVolumes(ev, volumes);
  const prim = ev.tracks.find((t) => t.parent_id === 0).ke_start;
  const tEnd = R.eventDuration(ev);
  let prevDep = 0;
  for (let i = 0; i <= 40; i++) {
    const time = tEnd * i / 40;
    const st = R.stateAt(ev, time, volumes.length);
    worst = Math.max(worst, Math.abs(st.deposited + st.inFlight + st.left - prim));
    monotone = monotone && st.deposited >= prevDep - 1e-12;
    prevDep = st.deposited;
    checked++;
  }
  const end = R.stateAt(ev, tEnd, volumes.length);
  expect(end.alive === 0 && end.inFlight === 0, 'event ' + ev.event_id + ': at the end no track is alive and nothing is in flight');
  const inLayers = end.byVolume.reduce((a, b) => a + b, 0);
  expect(Math.abs(inLayers - end.deposited) <= TOL, 'event ' + ev.event_id + ': the per-volume deposits add up to the total (' + (inLayers - end.deposited).toExponential(1) + ' GeV)');
  const first = R.headPosition(ev.tracks[0], ev.tracks[0].t[0]);
  expect(first && Math.abs(first[2] - ev.tracks[0].z[0]) < 1e-9 && R.headPosition(ev.tracks[0], tEnd + 1) === null,
    'event ' + ev.event_id + ': the head starts at the first point and is gone after the end');
  expect(R.interactionsBy(ev.interactions, tEnd) === ev.interactions.length && R.interactionsBy(ev.interactions, -1) === 0,
    'event ' + ev.event_id + ': all interactions have happened at the end, none before the start');
}
expect(worst <= TOL, 'deposited + in flight + left the world = primary energy at ' + checked + ' times: worst ' + worst.toExponential(2) + ' GeV, tolerance 5e-6 GeV');
expect(monotone, 'the deposited energy never decreases with time');

const ev0 = v2.events[0];
const tr = ev0.tracks[0];
const k = Math.floor(tr.n / 2), mid = 0.5 * (tr.t[k - 1] + tr.t[k]);
const h = R.headPosition(tr, mid);
expect(Math.abs(h[2] - 0.5 * (tr.z[k - 1] + tr.z[k])) < 1e-9, 'the head is interpolated linearly inside a step');
expect(R.depositFraction({ charge: 0 }, 0.5) === 0 && R.depositFraction({ charge: 0 }, 1) === 1 && R.depositFraction({ charge: -1 }, 0.5) === 0.5,
  'neutral tracks deposit at the end of a step, charged ones along it');

// ---- volumes
expect(R.findVolume(volumes, [100, 0, 0]) === 1 && R.findVolume(volumes, [75, 0, 0]) === -1 && R.findVolume(volumes, [100, 0, 600]) === -1,
  'findVolume: a point in a tube wall, in the gap, and beyond the half-length');
const boxes = [{ kind: 'box', min: [-50, -50, 0], max: [50, 50, 10] }, { kind: 'box', min: [-50, -50, 10], max: [50, 50, 15] }];
expect(R.findVolume(boxes, [0, 0, 5]) === 0 && R.findVolume(boxes, [0, 0, 12]) === 1 && R.findVolume(boxes, [0, 0, 20]) === -1, 'findVolume: boxes');

// ---- colours and text
const names = ['zzz', 'compt', 'Transportation', 'phot', 'aaa'];
const map = R.processColourMap(names), again = R.processColourMap(names);
expect(JSON.stringify(map) === JSON.stringify(again) && map.compt === '#e69f00' && map.zzz !== map.aaa, 'process colours are deterministic; known processes keep their colour');
const lo = R.energyColour(1e-7, 1), hi = R.energyColour(10, 1);
expect(R.energyFraction(1e-7, 1) === 0 && R.energyFraction(1, 1) === 1 && lo[0] < hi[0] && hi.every((c) => c >= 0 && c <= 1), 'the energy colour scale is clamped, log, and within 0..1');
expect(R.formatEnergy(1.5) === '1.500 GeV' && R.formatEnergy(0.0123) === '12.300 MeV' && R.formatEnergy(0.000045) === '45.0 keV', 'energies are written in GeV, MeV or keV');

process.exit(failed ? 1 : 0);
