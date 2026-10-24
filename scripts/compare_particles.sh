#!/usr/bin/env bash
# Runs an electron and a pion through the same calorimeter and plots event 0 of each,
# side by side.
#
#   scripts/compare_particles.sh [OUT_DIR] [THREADS]
#
# Needs the project environment to be active (micromamba activate vikshep-sim) and the
# program to be built (see README.md). Environment variables:
#   VIKSHEP_SIM   path of the vikshep-sim executable (default: build/vikshep-sim)
#   EVENT         the event to plot (default: 0)
#   PYTHON        the Python with pyarrow and matplotlib (default: python)
#
# Writes, in OUT_DIR (default: compare_particles_out; it must not exist):
#   electron/ pion/    the two run folders (hits.arrow, provenance.json, run_summary.json)
#   electron.txt pion.txt  the energy tables and OIDs; electron.log pion.log: Geant4's output
#   electron.png       event of the electron run
#   pion.png           event of the pion run
#   compare.png        both runs side by side, same vertical scale in the lower row
# Portable: bash 3.2, no GNU-only options.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)

out=${1:-compare_particles_out}
threads=${2:-4}
sim=${VIKSHEP_SIM:-$root/build/vikshep-sim}
event=${EVENT:-0}
python=${PYTHON:-python}

if [ ! -x "$sim" ]; then
  echo "compare_particles.sh: $sim not found; build the program first (README.md) or set VIKSHEP_SIM" >&2
  exit 2
fi
if [ -e "$out" ]; then
  echo "compare_particles.sh: $out already exists; give another folder or remove it yourself" >&2
  exit 2
fi
mkdir -p "$out"

for particle in electron pion; do
  spec="$root/docs/examples/${particle}_shower.json"
  echo "running the $particle spec ($spec)" >&2
  # stdout holds the energy table and the OID; Geant4's own output (stderr) goes to a log.
  if ! "$sim" dev-run --threads "$threads" --spec "$spec" --out "$out/$particle" \
        > "$out/$particle.txt" 2> "$out/$particle.log"; then
    echo "compare_particles.sh: the $particle run failed; see $out/$particle.log" >&2
    tail -n 5 "$out/$particle.log" >&2
    exit 1
  fi
  grep '^  oid' "$out/$particle.txt" >&2
done

plot="$here/plot_event.py"
"$python" "$plot" "$out/electron" --event "$event" --out "$out/electron.png"
"$python" "$plot" "$out/pion" --event "$event" --out "$out/pion.png"
"$python" "$plot" "$out/electron" "$out/pion" --event "$event" --out "$out/compare.png"
echo "done: $out/electron.png $out/pion.png $out/compare.png" >&2
