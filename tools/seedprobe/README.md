# seedprobe

A small program that measures how Geant4 seeds events in multithreaded mode. It
is a measurement tool, not part of vikshep-sim.

- It is **outside the build and CI**: the top-level `CMakeLists.txt` does not
  include it, and no workflow builds or runs it. It is built by hand.
- It does not use any code of this repository; it needs only Geant4.
- Results and how to build and run it are in `docs/notes-B4.md` (question 1).

Build and run (from the repository root, environment active):

```sh
cmake -S tools/seedprobe -B build-seedprobe -G Ninja \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DCMAKE_FIND_FRAMEWORK=LAST
cmake --build build-seedprobe
python3 tools/seedprobe/analyse.py build-seedprobe/seedprobe
```

The program itself (`seedprobe <events> <master seed> <threads> [MT|Tasking|Serial]`)
prints one line per event on stderr; `analyse.py` runs it several ways and
compares the engine states.
