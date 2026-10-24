#!/usr/bin/env python3
"""Reads an output folder of `vikshep-sim dev-run --out` with pyarrow and hashlib,
independently of the C++ code, and checks it against docs/HITS.md and docs/SIDECAR.md.

Usage: check_output_folder.py <folder> --sensitive absorber,active [--digests <file>]
"""
import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import pyarrow as pa
import pyarrow.ipc as ipc

parser = argparse.ArgumentParser()
parser.add_argument("folder")
parser.add_argument("--sensitive", required=True, help="comma separated names of the sensitive volumes")
parser.add_argument("--digests", help="event digest file of the same run")
args = parser.parse_args()

folder = Path(args.folder)
sensitive = sorted(args.sensitive.split(","))
problems = []


def expect(ok, text):
    print(("PASS  " if ok else "FAIL  ") + text)
    if not ok:
        problems.append(text)


expect(sorted(p.name for p in folder.iterdir()) == ["hits.arrow", "provenance.json", "run_summary.json"],
       "the folder holds exactly hits.arrow, provenance.json and run_summary.json")

summary = json.loads((folder / "run_summary.json").read_text())
provenance = json.loads((folder / "provenance.json").read_text())
raw = (folder / "hits.arrow").read_bytes()

# --- hits.arrow with pyarrow
r = ipc.open_file(folder / "hits.arrow")
schema = r.schema
names = ["event_id", "volume", "copy_no", "layer", "x", "y", "z", "t", "edep", "px", "py", "pz",
         "track_id", "parent_id", "pdg", "step"]
types = ["uint64", "dictionary", "int32", "int32"] + ["double"] * 8 + ["int32"] * 4
expect([f.name for f in schema] == names, "column names and order")
expect([("dictionary" if pa.types.is_dictionary(f.type) else str(f.type)) for f in schema] == types,
       "column types")
vt = schema.field("volume").type
expect(vt.index_type == pa.int32() and vt.value_type == pa.string() and not vt.ordered,
       "volume is dictionary<int32, utf8>, unordered")
expect(all(not f.nullable for f in schema), "no nullable field")
units = {f.name: (f.metadata or {}).get(b"unit", b"").decode() for f in schema}
expect(units == {"event_id": "", "volume": "", "copy_no": "", "layer": "", "x": "mm", "y": "mm", "z": "mm",
                 "t": "ns", "edep": "MeV", "px": "MeV/c", "py": "MeV/c", "pz": "MeV/c", "track_id": "",
                 "parent_id": "", "pdg": "", "step": ""}, "unit metadata of the dimensioned columns")
expect(schema.metadata == {b"vikshep.hits.schema_version": b"1.0"}, "schema metadata (schema_version 1.0)")

table = r.read_all()
rows = table.num_rows
batch_sizes = [r.get_batch(i).num_rows for i in range(r.num_record_batches)]
expect(all(n == 65536 for n in batch_sizes[:-1]) and all(0 < n <= 65536 for n in batch_sizes),
       "record batches: all 65536 rows except the last (%s)" % batch_sizes[-3:])
expect(rows == summary["hits_total"], "row count %d equals hits_total in run_summary.json" % rows)

if rows > 0:
    vol = table.column("volume").combine_chunks()
    expect(vol.dictionary.to_pylist() == sensitive, "dictionary %s equals the sorted sensitive volumes %s" %
           (vol.dictionary.to_pylist(), sensitive))
    ids = table.column("event_id").to_pylist()
    expect(all(ids[i] <= ids[i + 1] for i in range(len(ids) - 1)), "event_id never decreases")
    expect(r.stats.num_dictionary_batches == 1 and r.stats.num_dictionary_deltas == 0 and
           r.stats.num_replaced_dictionaries == 0, "the dictionary was written exactly once")
    distinct = len(set(ids))
else:
    distinct = 0
    expect(r.num_record_batches == 0, "no record batches")
    expect(len(schema) == 16, "the schema is still there")

# --- end-of-stream marker and footer layout
footer_size = struct.unpack_from("<i", raw, len(raw) - 10)[0]
expect(raw[:8] == b"ARROW1\0\0" and raw[-6:] == b"ARROW1", "ARROW1 magic at both ends")
expect(raw[len(raw) - 10 - footer_size - 8:len(raw) - 10 - footer_size] == bytes.fromhex("ffffffff00000000"),
       "end-of-stream marker before the footer")

# --- the OID, with hashlib
oid = hashlib.sha3_256(raw).hexdigest()[:28]
expect(summary["oid"] == oid, "run_summary.json oid %s equals the first 14 bytes of SHA3-256 of hits.arrow" % summary["oid"])
expect(len(oid) == 28 and oid == oid.lower(), "28 lowercase hex characters")
expect(summary["hits_file_bytes"] == len(raw), "hits_file_bytes equals the file size")

# --- counts
expect(summary["events_completed"] == summary["events_requested"], "all requested events completed")
expect(summary["events_with_hits"] == distinct, "events_with_hits %d equals the distinct event ids in the file" %
       summary["events_with_hits"])
expect(summary["events_with_hits"] + summary["events_without_hits"] == summary["events_requested"],
       "events with and without hits add up to the events requested")
expect(summary["hits_schema_version"] == "1.0" and summary["contract_version"] == 1, "schema and contract versions")

# --- provenance basics
expect(provenance["writer_scheme"] == 1 and provenance["seeding_scheme"] == 1, "writer_scheme and seeding_scheme are 1")
expect(provenance["nanoarrow"]["commit"] == "41c78c476817d1d122acc59ddacb7a3cbd00644c", "nanoarrow commit recorded")
expect(provenance["nanoarrow"]["version"] == "0.10.0-SNAPSHOT", "nanoarrow version recorded")
expect(provenance["spec"]["sensitive"] == sensitive, "the spec in provenance lists the sensitive volumes")
expect(len(provenance["spec_oid"]) == 28, "spec_oid has 28 characters")

# --- digests of the same run
if args.digests:
    lines = [json.loads(l) for l in Path(args.digests).read_text().splitlines()]
    expect([l["event_id"] for l in lines] == list(range(summary["events_requested"])), "digest file: one line per event, in order")
    if rows > 0:
        idx = table.column("volume").combine_chunks().indices.to_pylist()
        edep = table.column("edep").to_pylist()
        for k, name in enumerate(sensitive):
            from_file = math.fsum(e for e, i in zip(edep, idx) if i == k)
            from_digest = math.fsum(l["edep_by_volume_MeV"][name] for l in lines)
            expect(abs(from_file - from_digest) <= 1e-9 * max(1.0, abs(from_digest)),
                   "%s: summed edep from the file %.9g equals the digests' %.9g" % (name, from_file, from_digest))
            count_file = sum(1 for i in idx if i == k)
            count_digest = sum(l["hits_by_volume"][name] for l in lines)
            expect(count_file == count_digest, "%s: %d hits in the file equal %d in the digests" % (name, count_file, count_digest))
        per_event = {}
        for i in ids:
            per_event[i] = per_event.get(i, 0) + 1
        expect(all(per_event.get(l["event_id"], 0) == l["hits"] for l in lines),
               "hits per event in the file equal the digests (hit-free events absent)")
    else:
        expect(all(l["hits"] == 0 for l in lines), "digests: every event has 0 hits")

print()
if problems:
    print("%d checks failed" % len(problems))
    sys.exit(1)
print("output folder is valid")
