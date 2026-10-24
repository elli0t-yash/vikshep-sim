# nanoarrow spike

A feasibility test (milestone Y6): can nanoarrow's IPC component write
`hits.arrow` as described in docs/HITS.md? Not part of the build or CI.

`spike.cc` writes the 16-column schema of HITS.md (dictionary-encoded `volume`,
field metadata `unit`, schema metadata, non-nullable fields) as an Arrow IPC
file in batches of 65536 rows. `check.py` opens the result with pyarrow and
tests the criteria. Results are in STATUS.md (section "nanoarrow spike").

## Reproduce

nanoarrow is built from its own bundling script, in a fresh directory:

```sh
mkdir na && cd na
# released version 0.9.0 (tarball sha256 22c3edeaa5cb384c6da091f32a4e1c937779f2d62f84d646028de658b03d4385)
curl -sSL -o src.tar.gz https://github.com/apache/arrow-nanoarrow/archive/refs/tags/apache-arrow-nanoarrow-0.9.0.tar.gz
# or main at commit 41c78c476817d1d122acc59ddacb7a3cbd00644c, which reports version 0.10.0-SNAPSHOT
# (tarball sha256 418bd978ad204d57972ec8b32369a9a12071b0d2b7adb2f15ec83db819e2a5dc)
#   curl -sSL -o src.tar.gz https://github.com/apache/arrow-nanoarrow/archive/41c78c476817d1d122acc59ddacb7a3cbd00644c.tar.gz
mkdir src && tar -xzf src.tar.gz -C src --strip-components=1
python3 -I src/ci/scripts/bundle.py --output-dir bundle --with-ipc --with-flatcc
for f in nanoarrow nanoarrow_ipc flatcc; do cc -c -O1 -Ibundle/include bundle/src/$f.c -o $f.o; done
```

Build and run the spike (add `-DHAVE_DICT_BATCH` for the `main` commit, which has
`ArrowIpcWriterWriteDictionaryBatch`; add `-DWITH_EOS` to write the end-of-stream marker):

```sh
c++ -std=c++17 -O1 -DHAVE_DICT_BATCH -DWITH_EOS -Ibundle/include /path/to/spike.cc \
    nanoarrow.o nanoarrow_ipc.o flatcc.o -o spike
./spike out.arrow 200000 dict        # needs pyarrow for check.py:
python3 /path/to/check.py ./spike
```

With 0.9.0 the dictionary-encoded variant stops with
`ArrowIpcWriterWriteArrayView ... -> 45 (Cannot encode dictionary arrays)`;
`./spike out.arrow 100 nodict` (no dictionary column) works.
