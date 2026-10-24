#!/usr/bin/env python3
"""Checks the Arrow IPC files written by spike.cc against the criteria of the
feasibility spike (see README.md).

Usage: check.py <spike executable>      (needs pyarrow)
The executable is run as: <exe> <out.arrow> <rows> dict
"""
import hashlib
import re
import struct
import subprocess
import sys
import time

import pyarrow as pa
import pyarrow.ipc as ipc

exe = sys.argv[1]


def write(path, rows):
    subprocess.run([exe, path, str(rows), "dict"], check=True, capture_output=True)


def messages(path):
    """Walks the messages of an IPC file: [(type, offset, body bytes)], and returns
    also the offset of the footer and the 8 bytes just before it."""
    d = open(path, "rb").read()
    assert d[:8] == b"ARROW1\0\0" and d[-6:] == b"ARROW1"
    footer_size = struct.unpack_from("<i", d, len(d) - 10)[0]
    footer = len(d) - 10 - footer_size
    pos, found = 8, []
    while pos < footer:
        cont, length = struct.unpack_from("<Ii", d, pos)
        if cont != 0xFFFFFFFF or length == 0:
            break
        m = pa.ipc.read_message(pa.py_buffer(d[pos:footer]))
        body = m.body.size if m.body else 0
        found.append((m.type, pos, body))
        pos = (pos + 8 + length + body + 7) // 8 * 8
    return found, d[footer - 8:footer].hex()


problems = []


def expect(condition, text):
    print(("PASS  " if condition else "FAIL  ") + text)
    if not condition:
        problems.append(text)


# 1. pyarrow opens the file: schema, dictionary, values
write("c1.arrow", 200000)
r = ipc.open_file("c1.arrow")
schema = r.schema
t = r.read_all()
vol = t.column("volume").combine_chunks()
names = [f.name for f in schema]
expect(names == ["event_id", "volume", "copy_no", "layer", "x", "y", "z", "t", "edep", "px", "py",
                 "pz", "track_id", "parent_id", "pdg", "step"], "column names and order")
expect(str(schema.field("event_id").type) == "uint64", "event_id is uint64")
expect(schema.field("volume").type.index_type == pa.int32() and
       schema.field("volume").type.value_type == pa.string(), "volume is dictionary<int32, utf8>")
expect(vol.dictionary.to_pylist() == ["absorber", "active"], "dictionary values %s" % vol.dictionary.to_pylist())
expect(all(not f.nullable for f in schema), "all fields non-nullable")
units = {f.name: (f.metadata or {}).get(b"unit") for f in schema if f.metadata}
expect(units == {"x": b"mm", "y": b"mm", "z": b"mm", "t": b"ns", "edep": b"MeV", "px": b"MeV/c",
                 "py": b"MeV/c", "pz": b"MeV/c"}, "field metadata 'unit'")
expect(schema.metadata == {b"vikshep.hits.schema_version": b"1.0"}, "schema metadata %s" % schema.metadata)
expect(t.num_rows == 200000 and [r.get_batch(i).num_rows for i in range(r.num_record_batches)] ==
       [65536, 65536, 65536, 3392], "rows 200000 in batches of 65536, 65536, 65536, 3392")
expect(t.column("event_id").to_pylist()[:4] == [0, 0, 0, 1] and t.column("x").to_pylist()[:3] == [4.0, 4.5, 5.0],
       "values read back")

# 2. two writes are byte-identical (with a pause between them)
write("c2a.arrow", 200000)
time.sleep(2)
write("c2b.arrow", 200000)
a, b = open("c2a.arrow", "rb").read(), open("c2b.arrow", "rb").read()
expect(a == b == open("c1.arrow", "rb").read(), "three writes are byte-identical (%d bytes, sha256 %s...)" %
       (len(a), hashlib.sha256(a).hexdigest()[:16]))

# 3. nothing non-deterministic in the bytes: all printable strings
strings = [s.decode() for s in sorted(set(re.findall(rb"[ -~]{4,}", a)))]
print("      printable strings in the file:", strings)
expect(not any(re.search(r"20\d\d|time|date|host|user|/Users|/home|version", s) and s != "vikshep.hits.schema_version"
               for s in strings), "no timestamps, host names, paths or writer versions among them")

# 4. zero-batch file: opens in pyarrow, the dictionary is in the file
write("c4.arrow", 0)
r0 = ipc.open_file("c4.arrow")
kinds, before_footer = messages("c4.arrow")
expect(r0.num_record_batches == 0 and len(r0.schema) == 16 and r0.read_all().num_rows == 0,
       "zero-batch file opens: 0 batches, 16 columns, 0 rows")
expect([k[0] for k in kinds] == ["schema", "dictionary"], "zero-batch messages: %s" % [k[0] for k in kinds])

# 5. the dictionary is written exactly once
kinds, _ = messages("c1.arrow")
expect([k[0] for k in kinds].count("dictionary") == 1 and [k[0] for k in kinds].count("record batch") == 4,
       "200000 rows: messages %s" % [k[0] for k in kinds])
r.read_all()
st = r.stats
expect(st.num_dictionary_batches == 1 and st.num_dictionary_deltas == 0 and st.num_replaced_dictionaries == 0,
       "pyarrow read statistics: dictionary batches %d, deltas %d, replaced %d" %
       (st.num_dictionary_batches, st.num_dictionary_deltas, st.num_replaced_dictionaries))

# 6. end-of-stream marker before the footer (the format description puts one there)
expect(before_footer == "ffffffff00000000", "bytes before the footer: %s (EOS marker is ffffffff00000000)" % before_footer)

# 7. alignment: 101 rows -> an int32 column is 404 bytes; 8-byte alignment pads it to 408
write("c7.arrow", 101)
kinds, _ = messages("c7.arrow")
body = [k[2] for k in kinds if k[0] == "record batch"][0]
expect(body == 101 * 8 + 7 * 408 + 8 * 808, "record batch body %d bytes = 8-byte alignment" % body)
b0 = ipc.open_file("c7.arrow").get_batch(0)
expect(all(c.null_count == 0 and c.buffers()[0] is None for c in b0.columns), "no nulls, zero-length validity buffers")

print()
if problems:
    print("%d criteria failed" % len(problems))
    sys.exit(1)
print("all criteria passed")
