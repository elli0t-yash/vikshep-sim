#!/usr/bin/env python3
"""Cancellation of `vikshep-sim run` by signal (docs/SIDECAR.md, section 7).

Usage: check_cancel.py <vikshep-sim> <spec.json>
The spec should describe a run that lasts several seconds (many events).

Scenarios, each with its own output folder:
  1. SIGTERM right after the first progress line (done = 0, the run is starting)
  2. SIGTERM in the middle of the run (after at least 5 events were written)
  3. SIGINT in the middle of the run
  4. two signals in a row: the process must exit with code 4 at once (a temporary folder may remain)
For 1 to 3: exactly one error line CANCELLED with exit code 4, no output folder, no temporary folder.
Prints the time from the signal to the end of the process (the cancel latency) for each.
Exit code 77 (ctest: skipped) when the Geant4 data sets are missing.
"""
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

exe, spec = sys.argv[1], sys.argv[2]

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vikshep_testlib  # noqa: E402

vikshep_testlib.require_data_sets()

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


def start(out, threads=2):
    return subprocess.Popen([exe, "run", "--spec", spec, "--out", out, "--threads", str(threads), "--progress", "jsonl"],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)


def read_until(proc, predicate, timeout=60):
    """Reads stdout lines until predicate(line) is true; returns (lines, time of that line)."""
    lines = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = proc.stdout.readline()
        if not line:
            break
        lines.append(line)
        if predicate(json.loads(line)):
            return lines, time.monotonic()
    return lines, None


def finish(proc, timeout=60):
    rest = []
    try:
        for line in proc.stdout:
            rest.append(line)
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        problems.append("the process did not end within %d s" % timeout)
    return rest


def scenario(name, sig, condition):
    parent = tempfile.mkdtemp(prefix="vikshep-cancel-")
    out = os.path.join(parent, "run")
    try:
        proc = start(out)
        lines, _ = read_until(proc, condition)
        expect(bool(lines), "%s: got progress lines before signalling" % name)
        sent = time.monotonic()
        proc.send_signal(sig)
        rest = finish(proc)
        latency = time.monotonic() - sent
        all_lines = [json.loads(l) for l in lines + rest]
        errors = [l for l in all_lines if l.get("event") == "error"]
        expect(proc.returncode == 4, "%s: exit code 4 (got %s)" % (name, proc.returncode))
        expect(len(errors) == 1 and errors[0].get("code") == "CANCELLED",
               "%s: exactly one error line, code CANCELLED (%s)" % (name, [e.get("code") for e in errors]))
        expect(all_lines[-1].get("event") == "error", "%s: the error line is the last line" % name)
        expect(not any(l.get("event") == "done" for l in all_lines), "%s: no done line" % name)
        expect(not os.path.exists(out), "%s: no output folder" % name)
        expect(os.listdir(parent) == [], "%s: no temporary folder left (%s)" % (name, os.listdir(parent)))
        print("      %s: cancel latency (signal to end of process): %.0f ms; last progress done=%s" %
              (name, 1000 * latency,
               next((l["done"] for l in reversed(all_lines) if l.get("event") == "progress"), None)))
        return latency
    finally:
        shutil.rmtree(parent, ignore_errors=True)


lat = {}
lat["SIGTERM at start"] = scenario("SIGTERM at start", signal.SIGTERM, lambda l: l.get("event") == "progress")
lat["SIGTERM mid-run"] = scenario("SIGTERM mid-run", signal.SIGTERM, lambda l: l.get("event") == "progress" and l["done"] >= 5)
lat["SIGINT mid-run"] = scenario("SIGINT mid-run", signal.SIGINT, lambda l: l.get("event") == "progress" and l["done"] >= 5)

# two signals: the first is handled by the flag, the second must end the process at once.
# (Two signals sent back to back can be merged by the kernel into one, so the second is sent
# 0.2 s after the first, while Geant4 is still initialising and the process is busy.)
parent = tempfile.mkdtemp(prefix="vikshep-cancel-")
out = os.path.join(parent, "run")
try:
    proc = start(out)
    lines, _ = read_until(proc, lambda l: l.get("event") == "progress")
    proc.send_signal(signal.SIGTERM)
    time.sleep(0.2)
    if proc.poll() is not None:
        print("      double signal: the process had already ended 0.2 s after the first signal; "
              "the second-signal path was NOT exercised on this machine")
        finish(proc)
    else:
        sent = time.monotonic()
        proc.send_signal(signal.SIGTERM)
        rest = finish(proc)
        latency = time.monotonic() - sent
        expect(proc.returncode == 4, "double signal: exit code 4 (got %s)" % proc.returncode)
        expect(latency < 0.5, "double signal: the process ended at once, %.0f ms after the second signal" % (1000 * latency))
        expect(not os.path.exists(out), "double signal: no output folder")
        parsed = [json.loads(l) for l in rest]
        expect(not any(l.get("event") == "error" for l in parsed),
               "double signal: no further output after the second signal (%d lines)" % len(parsed))
        leftovers = os.listdir(parent)
        print("      double signal: leftover entries (a temporary folder may remain, SIDECAR S4): %s" % leftovers)
        expect(all(name.startswith(".run.partial-") for name in leftovers), "double signal: leftovers are only temporary folders")
finally:
    shutil.rmtree(parent, ignore_errors=True)

for name, value in lat.items():
    expect(value < 5.0, "%s: latency %.0f ms is under 5 s" % (name, 1000 * value))

print()
if problems:
    print("%d checks failed" % len(problems))
    sys.exit(1)
print("cancellation works")
