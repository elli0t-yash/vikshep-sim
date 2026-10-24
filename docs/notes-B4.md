# Notes on Geant4 example B4

Status: the two questions below were first written as open. The sections
marked "measured by an automated coding agent, to be re-run by Yash" were filled in on 2026-10-23 from
a small probe program (tools/seedprobe), not from example B4 itself. They are
measurements on one machine (macOS arm64, Geant4 11.4.3, conda build
noqt_429d0ab_0) and have not been re-run by the owner. The B4 runs themselves
are still to be done.

## Questions

1. How does Geant4 seed events per thread in multithreaded mode?
2. How will we make the output independent of thread count?

## Why these matter

HITS.md requires `hits.arrow` to be ordered by event and, within an event, by
step order, with a result that does not depend on the number of worker threads.
That only holds if the random number stream of an event depends on the run
seed and the event id and not on the thread that ran it. Question 1 decides
whether that is already true; question 2 is what we do if it is not.

## To record for each run

- Date (from `date -u`), machine, operating system, architecture.
- Geant4 version and conda build string (`micromamba list geant4`).
- Exact command, number of threads, seed settings, number of events.
- What was compared (for example hit lists from 1 thread against N threads),
  and how.
- What was observed, copied from output, not paraphrased.

## Question 1: per-thread event seeding

Measured by an automated coding agent on 2026-10-23, to be re-run by Yash.

### How to re-run

```sh
micromamba activate vikshep-sim
cmake -S tools/seedprobe -B build-seedprobe -G Ninja \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build-seedprobe
python3 tools/seedprobe/analyse.py build-seedprobe/seedprobe
```

The probe runs cheap events (an electron through vacuum) with Geant4's own
seeding and prints, for each event, the global event id the worker sees, the
worker thread and a hash of the thread's random engine state at primary
generation and at the start of the event action. It draws no random numbers.
`analyse.py` compares the states between runs.

### Observations (G4MTRunManager, default settings)

- Manager settings: `SeedOncePerCommunication = 0` (seeds are made per event),
  `EventModuloDef = 0`, `nSeedsPerEvent = 2` and `nSeedsMax = 10000` (members of
  G4MTRunManager, include/Geant4/G4MTRunManager.hh). The master engine
  generates the seed pairs; workers receive them with the events.
- The event id a worker sees is the global event id (0 to N-1). It is not a
  per-thread counter.
- The engine state at primary generation equals the state at the start of the
  event action: Geant4 reseeds the thread engine before the primary vertex
  is generated, and nothing draws a number in between.
- Events are handed to workers in small blocks of consecutive ids (pairs, in
  the runs of 2026-10-23), so different threads run different events and which thread runs
  event k varies.
- Engine state of event k, compared between runs with the same master seed:
  identical with 1 thread and with 4 threads; identical in a 20-event and a
  200-event run; identical for events 0 to 4 of a 5-event run (3 threads) and a
  20-event run; identical for events 0 to 10009 of a 10010-event run (1 thread)
  against a 20000-event run (4 threads), i.e. across Geant4's refill of seeds at
  10000; identical for events 0 to 19999 of a 20000-event run (4 threads) and a
  25000-event run (3 threads). A different master seed (42 against 43) gave
  different states for all 20 events compared.
- G4TaskRunManager ("Tasking") gave the same states as G4MTRunManager for the
  same seed and events (20 events, 4 threads).
- The serial run manager (G4RunManager) gave **different** states from
  G4MTRunManager with one thread, for the same master seed. A serial run and a
  multithreaded run do not simulate the same events.

So, on the evidence above, the default multithreaded seeding already depends
only on (master seed, global event id). That is a property of how G4MTRunManager
draws its seeds (in event order, from the master engine, once per event), not
something the program enforces, and it holds only while these settings stay
as they are. UNCLEAR: it was not tested with `SetEventModulo` or
`SetSeedOncePerCommunication` changed, with a magnetic field, or on Linux.

## Question 2: output independent of thread count

Implemented by an automated coding agent on 2026-10-23, to be reviewed by Yash.

Options considered:

1. Rely on Geant4's default seeding. It behaved correctly in every measurement
   above, but nothing in our code would stop it changing (a Geant4 update, a
   changed run manager setting, a switch to the serial manager).
2. Change the run manager settings (`SetSeedOncePerCommunication`, event
   modulo). More settings that must stay right; rejected.
3. Reseed the thread's engine at the start of every event from a defined hash of
   (spec seed, global event id), overwriting Geant4's seeds. Chosen.

What was built:

- `src/event_seed.cc`: the hash (SplitMix64-based; defined in HITS.md section
  4.2) and the reseeding call, used by the event action in `src/dev_run.cc`.
  Reference values are pinned in tests/test_event_seed.cc.
- A reorder buffer (`src/reorder_buffer.hh`) so that hits leave the simulation in
  event order whatever order the threads finish in.
- The reseeding is the first step of GeneratePrimaries(), so it comes before any
  draw, including draws by the gun itself. (An earlier version reseeded in the
  event action, which relied on the gun drawing nothing.) A test-only gun that draws
  a random number gives the same number for 1 and 4 threads
  (tests/check_random_gun.py).
- Measured on CLHEP's MixMax engine: the upper 32 bits of the second seed are
  ignored (tests/test_event_seed.cc), so the second seed is defined as a 32-bit
  value.

Evidence (tests, macOS arm64): digests of every event are identical with 1 and
with 4 threads; events 0 to 4 of a 5-event run equal events 0 to 4 of a 20-event
run; a different seed gives different events. A deliberate mutation (seed also
depending on the thread id) made the thread-count and run-independence tests fail,
so the tests can detect the dependence they are meant to rule out.

Not shown: identity across operating systems or builds (never claimed), and
behaviour with a different run manager type than G4MTRunManager (the serial
manager is excluded by the measurement above and is not used).

Decision: (to be confirmed by the owner)

## Effect on HITS.md and SIDECAR.md

HITS.md section 4.2 (new) defines the per-event seeding, and section 4 now states
the thread-count independence as a property of that definition. SIDECAR.md is
unchanged.
