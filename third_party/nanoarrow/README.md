# Vendored nanoarrow (with its IPC component and the flatcc runtime)

Used to write `hits.arrow` (Arrow IPC file format). See docs/HITS.md.

| Item | Value |
|------|-------|
| Project | Apache Arrow nanoarrow, https://github.com/apache/arrow-nanoarrow |
| Commit | `41c78c476817d1d122acc59ddacb7a3cbd00644c` (branch main, 2026-10-24) |
| Version string in the code | `0.10.0-SNAPSHOT` (an unreleased snapshot, 39 commits after release 0.9.0) |
| Source tarball sha256 | `418bd978ad204d57972ec8b32369a9a12071b0d2b7adb2f15ec83db819e2a5dc` |
| Licence | Apache-2.0 (`LICENSE.txt`, `NOTICE.txt`) |
| Bundled flatcc runtime | Apache-2.0, Copyright Mikkel F. Jorgensen, dvide.com (`LICENSE.flatcc.txt`); the headers in `include/flatcc/portable/` carry their own licence text in each file (for example `pstdint.h`, BSD) |

Why a snapshot and not release 0.9.0: release 0.9.0 cannot write a
dictionary-encoded column (`ArrowIpcWriterWriteArrayView` returns "Cannot encode
dictionary arrays") and has no function to write a dictionary batch. The spike
that shows this is in `tools/nanoarrow_spike`; results are in STATUS.md.

## How these files were produced

The files are the output of nanoarrow's own bundling script, unmodified:

```sh
curl -sSL -o src.tar.gz https://github.com/apache/arrow-nanoarrow/archive/41c78c476817d1d122acc59ddacb7a3cbd00644c.tar.gz
mkdir src && tar -xzf src.tar.gz -C src --strip-components=1
python3 -I src/ci/scripts/bundle.py --output-dir bundle --with-ipc --with-flatcc
```

Copied from `bundle/`: `include/nanoarrow/nanoarrow.h`,
`include/nanoarrow/nanoarrow_ipc.h`, `include/flatcc/`, `src/nanoarrow.c`,
`src/nanoarrow_ipc.c`, `src/flatcc.c`. The C++ wrapper headers (`*.hpp`) were left out.
`LICENSE.txt` and `NOTICE.txt` come from the tarball root, `LICENSE.flatcc.txt`
from `thirdparty/flatcc/LICENSE`.

## Updating

Changing this code can change the bytes of every `hits.arrow`, hence every OID.
Any update must pass the golden-file test (tests/test_hits_writer.cc), and if the
bytes change, `writer_scheme` (src/versions.hh, docs/HITS.md) must be bumped.
