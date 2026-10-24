#!/usr/bin/env python3
"""A random number drawn right after an event is seeded must not depend on the
thread count, on the number of events, or on the other events of the run.

Usage: check_random_gun.py <random-gun-probe>
Exit code 77 (ctest: skipped) when the probe reports missing Geant4 data sets.
"""
import subprocess
import sys

exe = sys.argv[1]


def run(threads, events, seed=42):
    proc = subprocess.run([exe, str(threads), str(events), str(seed)], capture_output=True, text=True)
    if proc.returncode in (77, 1) and "data sets are missing" in proc.stderr:
        sys.stderr.write(proc.stderr)  # the probe says skipped (77) or, with VIKSHEP_REQUIRE_DATA, failed (1)
        sys.exit(proc.returncode)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr[-2000:])
        sys.exit("probe failed with exit code %d" % proc.returncode)
    rows = [line.split() for line in proc.stdout.splitlines()]
    return {int(r[0]): (r[1], r[2]) for r in rows}


one = run(1, 20)
four = run(4, 20)
short = run(3, 5)
other = run(4, 20, seed=43)

problems = []
for e, (got, expected) in one.items():
    if got != expected:
        problems.append("event %d: drawn %s but a freshly seeded engine gives %s" % (e, got, expected))
if [one[e][0] for e in sorted(one)] != [four[e][0] for e in sorted(four)]:
    problems.append("1 thread and 4 threads drew different numbers")
for e in range(5):
    if short[e][0] != one[e][0]:
        problems.append("event %d differs between a 5-event and a 20-event run" % e)
if len({v[0] for v in one.values()}) != 20:
    problems.append("the 20 events did not draw 20 different numbers")
if any(other[e][0] == one[e][0] for e in one):
    problems.append("seed 43 repeated a number of seed 42")

print("20 events: drawn values equal expected for all:", all(g == x for g, x in one.values()))
print("1 thread vs 4 threads identical:", [one[e][0] for e in sorted(one)] == [four[e][0] for e in sorted(four)])
if problems:
    print("\n".join(problems))
    sys.exit(1)
print("random draws are independent of threads, event count and the other events")
