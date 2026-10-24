#!/usr/bin/env python3
"""The stdout of `vikshep-sim run --progress jsonl`: shape, order and cadence
(docs/SIDECAR.md, section 2).

Usage: check_run_stream.py <vikshep-sim> <spec.json> <threads>
Exit code 77 (ctest: skipped) when the Geant4 data sets are missing.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

exe, spec, threads = sys.argv[1], sys.argv[2], sys.argv[3]

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vikshep_testlib  # noqa: E402

vikshep_testlib.require_data_sets()

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


parent = tempfile.mkdtemp(prefix="vikshep-stream-")
out = os.path.join(parent, "run")
try:
    proc = subprocess.Popen([exe, "run", "--spec", spec, "--out", out, "--threads", threads, "--progress", "jsonl"],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    stamped = []
    for line in proc.stdout:
        stamped.append((time.monotonic(), line))
    code = proc.wait()
    expect(code == 0, "exit code 0 (got %d)" % code)

    # --- a. purity: every line is a JSON object with a known event
    messages = []
    pure = True
    for _, line in stamped:
        try:
            m = json.loads(line)
            pure = pure and isinstance(m, dict) and line.endswith("\n") and "\n" not in line[:-1]
            pure = pure and m.get("event") in ("progress", "done", "error")
            messages.append(m)
        except json.JSONDecodeError:
            pure = False
    expect(pure and len(messages) >= 3, "every stdout line is one JSON object with a known event (%d lines)" % len(messages))
    expect(all(list(m.keys()) == sorted(m.keys()) for m in messages), "keys are sorted in every line")

    # --- b. shape
    progress = [(t, json.loads(l)) for t, l in stamped if json.loads(l)["event"] == "progress"]
    dones = [json.loads(l) for _, l in stamped if json.loads(l)["event"] == "done"]
    errors = [json.loads(l) for _, l in stamped if json.loads(l)["event"] == "error"]
    total = progress[0][1]["total"]
    expect(progress[0][1]["done"] == 0, "the first progress line has done = 0")
    expect(progress[-1][1]["done"] == total, "the last progress line has done = total (%d)" % total)
    values = [p["done"] for _, p in progress]
    expect(all(values[i] <= values[i + 1] for i in range(len(values) - 1)), "done never decreases: %s" % values)
    expect(all(p["total"] == total and 0 <= p["done"] <= total for _, p in progress), "total is constant and done <= total")
    expect(len(dones) == 1 and not errors, "exactly one done line and no error line")
    expect(json.loads(stamped[-1][1])["event"] == "done", "the done line is the last line")
    expect(json.loads(stamped[-2][1])["event"] == "progress" and json.loads(stamped[-2][1])["done"] == total,
           "the last progress line comes right before the done line")
    summary = json.load(open(os.path.join(out, "run_summary.json")))
    expect(dones[0]["oid"] == summary["oid"], "the oid of the done line equals run_summary.json's (%s)" % summary["oid"])
    expect(len(dones[0]["oid"]) == 28, "the oid has 28 characters")
    expect(os.path.isdir(out) and sorted(os.listdir(out)) == ["hits.arrow", "provenance.json", "run_summary.json"],
           "the output folder is complete when the done line has been read")

    # --- cadence: at most one line per 250 ms, except the first and the last line
    gaps = [(progress[i + 1][0] - progress[i][0], i + 1) for i in range(len(progress) - 1)]
    inner = [g for g, index in gaps if index != len(progress) - 1]
    print("      progress lines: %d; gaps between them (s): %s" % (len(progress), ["%.3f" % g for g, _ in gaps]))
    expect(all(g >= 0.24 for g in inner), "no two progress lines closer than 250 ms (apart from the last): smallest %.3f s" %
           (min(inner) if inner else 9.9))
    expect(len(progress) >= 3, "the run was long enough to show throttled progress (%d lines)" % len(progress))

    # --- --progress none: stdout stays empty
    out2 = os.path.join(parent, "run2")
    quiet = subprocess.run([exe, "run", "--spec", spec, "--out", out2, "--threads", threads, "--progress", "none"],
                           capture_output=True, text=True)
    expect(quiet.returncode == 0 and quiet.stdout == "", "--progress none: exit 0 and stdout is empty (%d bytes)" % len(quiet.stdout))
    expect(os.path.isdir(out2), "--progress none: the output folder exists")
    expect(b"Geant4" in quiet.stderr.encode() or len(quiet.stderr) > 0, "--progress none: Geant4's output went to stderr")
finally:
    shutil.rmtree(parent, ignore_errors=True)

print()
if problems:
    print("%d checks failed" % len(problems))
    sys.exit(1)
print("the stdout stream is as specified")
