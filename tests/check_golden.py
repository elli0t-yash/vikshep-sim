#!/usr/bin/env python3
"""Checks the golden hits files with pyarrow and hashlib, independently of the C++ writer.

Usage: check_golden.py <golden dir>               check
       check_golden.py <golden dir> --write-oids  rewrite OIDS.txt from the golden files

OIDS.txt holds the OID (first 14 bytes of SHA3-256, 28 lowercase hex characters) of each
golden file, computed here with Python's hashlib. The C++ test compares the OID it
computes with these.
"""
import hashlib
import sys
from pathlib import Path

import pyarrow as pa
import pyarrow.ipc as ipc

golden = Path(sys.argv[1])
write_oids = "--write-oids" in sys.argv


def oid(path):
    return hashlib.sha3_256(path.read_bytes()).hexdigest()[:28]


names = ["hits_small.arrow", "hits_empty.arrow"]
if write_oids:
    (golden / "OIDS.txt").write_text("".join("%s %s\n" % (n, oid(golden / n)) for n in names))
    print("wrote", golden / "OIDS.txt")
    sys.exit(0)

problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


recorded = dict(line.split() for line in (golden / "OIDS.txt").read_text().splitlines())
for n in names:
    expect(recorded.get(n) == oid(golden / n), "%s: OID in OIDS.txt equals hashlib.sha3_256 of the file" % n)


def hit(volume, layer, k):
    return dict(volume=["absorber", "active"][volume], copy_no=layer, layer=layer, x=0.25 * k, y=-0.5 * k,
                z=-75.0 + 1.5 * k, t=0.125 * k, edep=0.0625 * (k + 1), px=8.0 * k, py=-2.0, pz=100.0 + k,
                track_id=1 + k % 3, parent_id=0 if k % 3 == 0 else 1, pdg=11 if k % 2 == 0 else 22, step=1 + k)


# the dataset of tests/test_hits_writer.cc (small_dataset), written independently here
expected = []
for event_id, hits in [(0, [(0, 0, 0), (1, 0, 1), (1, 1, 2)]), (1, []),
                       (2, [(0, 2, 3), (0, 2, 4), (1, 3, 5), (1, 3, 6)]), (3, []),
                       (4, [(0, 9, 7), (1, 9, 8), (1, 9, 9)])]:
    for v, layer, k in hits:
        expected.append(dict(event_id=event_id, **hit(v, layer, k)))

r = ipc.open_file(golden / "hits_small.arrow")
t = r.read_all()
expect([r.get_batch(i).num_rows for i in range(r.num_record_batches)] == [4, 4, 2], "small: batches of 4, 4, 2 rows")
expect(t.num_rows == 10, "small: 10 rows (two hit-free events absent)")
rows = t.to_pylist()
expect(len(rows) == len(expected) and all(rows[i] == expected[i] for i in range(len(expected))),
       "small: every value equals the independently computed dataset")
expect(t.column("event_id").to_pylist() == [0, 0, 0, 2, 2, 2, 2, 4, 4, 4], "small: event ids in order, no hit-free events")
expect(t.schema.metadata == {b"vikshep.hits.schema_version": b"1.0"}, "small: schema metadata")
st = ipc.open_file(golden / "hits_small.arrow")
st.read_all()
expect(st.stats.num_dictionary_batches == 1 and st.stats.num_dictionary_deltas == 0, "small: one dictionary batch")

r0 = ipc.open_file(golden / "hits_empty.arrow")
t0 = r0.read_all()
expect(r0.num_record_batches == 0 and t0.num_rows == 0 and len(t0.schema) == 16, "empty: no batches, 0 rows, 16 columns")
expect(t0.schema.field("volume").type == pa.dictionary(pa.int32(), pa.string()), "empty: volume is dictionary<int32, utf8>")

if problems:
    print("%d checks failed" % len(problems))
    sys.exit(1)
print("golden files are valid and independent of the C++ writer's own code")
