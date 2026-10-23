#!/usr/bin/env python3
"""Runs seedprobe several ways and compares the per-event engine states.

Usage: analyse.py <path to seedprobe>
Prints one line per question. "True" means the engine state of the event was
identical in the compared runs.
"""
import collections
import re
import subprocess
import sys

exe = sys.argv[1]


def run(events, seed, threads, manager="MT"):
    err = subprocess.run([exe, str(events), str(seed), str(threads), manager],
                         capture_output=True, text=True).stderr
    gen, begin = {}, {}
    threads_of = collections.defaultdict(set)
    manager_line = ""
    for line in err.splitlines():
        if line.startswith("SEEDPROBE manager"):
            manager_line = line
        m = re.match(r"SEEDPROBE (gen|begin)\s+event=(\d+) thread=(-?\d+) state=(\w+)", line)
        if not m:
            continue
        kind, event, thread, state = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
        (gen if kind == "gen" else begin)[event] = state
        threads_of[event].add(thread)
    return gen, begin, threads_of, manager_line


def same(a, b, ids):
    return all(i in a and i in b and a[i] == b[i] for i in ids)


a = run(20, 42, 1)
b = run(20, 42, 4)
c = run(200, 42, 4)
d = run(20, 43, 1)
e = run(5, 42, 3)
f = run(10010, 42, 1)
g = run(20000, 42, 4)
h = run(25000, 42, 3)
t = run(20, 42, 4, "Tasking")
s = run(20, 42, 1, "Serial")

print("manager settings:", a[3])
print("event ids seen by workers (20 events, 4 threads):", sorted(b[1]) == list(range(20)))
print("engine state at primary generation == at BeginOfEventAction:",
      all(b[0][i] == b[1][i] for i in b[0]))
print("20 distinct states for 20 events:", len(set(b[1].values())) == 20)
print("1 thread vs 4 threads, 20 events:", same(a[1], b[1], range(20)))
print("20-event run vs 200-event run, events 0-19:", same(b[1], c[1], range(20)))
print("5-event run (3 threads) vs 20-event run, events 0-4:", same(e[1], a[1], range(5)))
print("seed 42 vs seed 43, events equal:", sum(a[1][i] == d[1][i] for i in range(20)), "of 20")
print("10010 events/1 thread vs 20000 events/4 threads, events 0-10009:",
      same(f[1], g[1], range(10010)))
print("20000 events/4 threads vs 25000 events/3 threads, events 0-19999:",
      same(g[1], h[1], range(20000)))
print("Tasking manager (4 thr) vs MT (4 thr), 20 events:", same(t[1], b[1], range(20)))
print("Serial manager vs MT (1 thread), 20 events:", same(s[1], a[1], range(20)))
print("worker threads that ran events (200 events, 4 threads):",
      sorted(set().union(*c[2].values())))
print("worker thread per event, events 0-7 (20 events, 4 threads):",
      {i: sorted(b[2][i]) for i in range(8)})
