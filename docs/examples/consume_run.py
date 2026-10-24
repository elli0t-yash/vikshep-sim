#!/usr/bin/env python3
"""A reference consumer of `vikshep-sim run`: shows what the desktop app has to do.

    python docs/examples/consume_run.py --spec docs/examples/calorimeter.json --out my_run [--threads 4]

It starts `vikshep-sim run --progress jsonl`, reads stdout line by line, draws a text
progress bar, and when the run is done prints the OID and the listing of the output
folder. The rules it follows are those of docs/SIDECAR.md:

  * stdout carries only JSON lines (one object per line); stderr is for people and is
    not parsed (here it is passed through to the terminal, or hidden with --quiet);
  * a line has an "event": "progress", "done" or "error"; unknown events and unknown
    keys are ignored (the contract may add them);
  * a run ends with exactly one "done" line (exit code 0) or one "error" line (exit code
    not 0); a process that ends with neither was killed or crashed, and is a failure;
  * "done" is written only after the output folder is in place; before that the folder
    does not exist, so nothing is read from it earlier;
  * the exit code is the authority: 1 simulation or internal error, 2 usage or invalid
    spec, 3 Geant4 data sets missing, 4 cancelled;
  * to cancel, send SIGTERM (here: Ctrl-C in this script sends it) and wait for the
    "error" line with code CANCELLED and exit code 4.

The exit code of this script is the exit code of vikshep-sim.
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys

BAR_WIDTH = 40


def draw_bar(done, total, stream):
    filled = BAR_WIDTH * done // total if total else BAR_WIDTH
    bar = "#" * filled + "-" * (BAR_WIDTH - filled)
    text = "[%s] %d/%d events" % (bar, done, total)
    if stream.isatty():
        stream.write("\r" + text)
        stream.flush()
    else:
        stream.write(text + "\n")


def main():
    parser = argparse.ArgumentParser(description="Run vikshep-sim and show its progress.")
    parser.add_argument("--spec", required=True)
    parser.add_argument("--out", required=True, help="output folder; must not exist")
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--exe", default=os.environ.get("VIKSHEP_SIM", "build/vikshep-sim"))
    parser.add_argument("--quiet", action="store_true", help="hide the program's stderr")
    args = parser.parse_args()

    command = [args.exe, "run", "--spec", args.spec, "--out", args.out, "--threads", str(args.threads),
               "--progress", "jsonl"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL if args.quiet else None,
                               text=True)

    # Ctrl-C here cancels the run: forward it as SIGTERM, then keep reading until the end.
    signal.signal(signal.SIGINT, lambda *_: process.send_signal(signal.SIGTERM))

    oid = None
    error = None
    last_total = 0
    for line in process.stdout:
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            print("\nprotocol violation: a stdout line that is not JSON: %r" % line, file=sys.stderr)
            continue
        kind = message.get("event")
        if kind == "progress":
            last_total = message["total"]
            draw_bar(message["done"], message["total"], sys.stdout)
        elif kind == "done":
            oid = message["oid"]
        elif kind == "error":
            error = message
        # any other event: ignored (a later version of the contract may add some)
    code = process.wait()
    if sys.stdout.isatty() and last_total:
        sys.stdout.write("\n")

    if oid is not None and code == 0:
        print("done. OID %s" % oid)
        print("output folder %s:" % args.out)
        for name in sorted(os.listdir(args.out)):
            print("  %-18s %12d bytes" % (name, os.path.getsize(os.path.join(args.out, name))))
        return 0
    if error is not None:
        print("failed: %s (exit code %d): %s" % (error.get("code"), code, error.get("message")), file=sys.stderr)
        for problem in error.get("errors", []):  # present for SPEC_INVALID
            where = problem.get("pointer", "")
            if "missing" in problem:
                where += "/" + problem["missing"]
            print("  %s: %s" % (where or "(root)", problem.get("message")), file=sys.stderr)
        return code or 1
    # no done and no error line: the process was killed or crashed
    print("the process ended without a done or error line (exit code %d): treat as a failure" % code,
          file=sys.stderr)
    return code or 1


if __name__ == "__main__":
    sys.exit(main())
