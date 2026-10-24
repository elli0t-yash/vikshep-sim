#!/usr/bin/env python3
"""The fast-math guard.

1. Configuring with -ffast-math or -Ofast (in any of the flag variables) must fail, with a clear message.
2. The compile commands of the real build must contain neither, and in a Release build every command
   for our own sources must contain -O3 and -DNDEBUG (the Release flags).

Usage: check_fast_math.py <cmake> <source dir> <build dir>
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

cmake, source, build = sys.argv[1], sys.argv[2], sys.argv[3]
problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


bad = [
    ("CMAKE_CXX_FLAGS=-ffast-math", ["-DCMAKE_CXX_FLAGS=-ffast-math"]),
    ("CMAKE_CXX_FLAGS=-Ofast", ["-DCMAKE_CXX_FLAGS=-Ofast"]),
    ("CMAKE_C_FLAGS=-ffast-math", ["-DCMAKE_C_FLAGS=-ffast-math"]),
    ("CMAKE_CXX_FLAGS_RELEASE=-O3 -ffast-math", ["-DCMAKE_CXX_FLAGS_RELEASE=-O3 -ffast-math"]),
    ("CMAKE_CXX_FLAGS=-funsafe-math-optimizations", ["-DCMAKE_CXX_FLAGS=-funsafe-math-optimizations"]),
]
for name, args in bad:
    tmp = tempfile.mkdtemp(prefix="vikshep-fastmath-")
    try:
        proc = subprocess.run([cmake, "-S", source, "-B", os.path.join(tmp, "b")] + args, capture_output=True, text=True)
        message = proc.stderr
        expect(proc.returncode != 0, "configuring with %s fails (exit code %d)" % (name, proc.returncode))
        expect("are not allowed" in message and "floating-point" in message,
               "the message says why: %s" % " ".join(message.split())[:150])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

# the real build
with open(os.path.join(build, "compile_commands.json")) as f:
    commands = json.load(f)
ours = [c for c in commands if c["file"].startswith(source) and "/third_party/" not in c["file"]]
expect(len(commands) > 20, "the build has %d compile commands" % len(commands))
forbidden = re.compile(r"fast-math|Ofast|unsafe-math|finite-math-only|associative-math")
expect(not any(forbidden.search(c["command"]) for c in commands), "no compile command contains a fast-math option")

cache = open(os.path.join(build, "CMakeCache.txt")).read()
build_type = re.search(r"^CMAKE_BUILD_TYPE:STRING=(.*)$", cache, re.M).group(1)
print("      build type of this build: %s; %d of our own compile commands" % (build_type, len(ours)))
if build_type == "Release":
    expect(all("-O3" in c["command"] and "-DNDEBUG" in c["command"] for c in ours),
           "every command for our own sources has the Release flags -O3 -DNDEBUG")
    sample = ours[0]["command"]
    print("      example: ... %s" % " ".join(t for t in sample.split() if t.startswith("-O") or t.startswith("-DNDEBUG") or t.startswith("-std")))

print()
if problems:
    print("%d checks failed" % len(problems))
    sys.exit(1)
print("fast-math is refused and absent")
