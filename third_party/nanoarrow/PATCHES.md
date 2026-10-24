# Changes made to the vendored nanoarrow

The files in `src/` and `include/` are the output of nanoarrow's bundling script (see README.md), with
the changes listed here and no others. Each change is the smallest that fixes its problem. Every
change must be reported to the nanoarrow project and removed when the pinned commit moves to a
version that has the fix.

## Patch 1: zero the padding of the footer's Block structs

- **File and function:** `src/nanoarrow_ipc.c`, `ArrowIpcEncoderEncodeFooter`, the two loops that fill
  the `recordBatches` and `dictionaries` vectors of the Arrow IPC footer.
- **Problem:** the flatcc struct `org_apache_arrow_flatbuf_Block` is
  `{ int64_t offset; int32_t metaDataLength; int64_t bodyLength; }`: the C layout has **4 bytes of
  padding** after `metaDataLength`. The code built a local `struct ns(Block) block = {offset,
  metadata_length, body_length}` and copied it into the footer buffer. The C standard leaves the
  padding of a brace-initialised struct indeterminate, so whatever was on the stack was copied into
  the footer, and the bytes of the file differed between builds and runs. Found on Linux x86-64 with gcc 15.3
  and no optimisation: the dictionary block's padding was `55 55 00 00` instead of `00 00 00 00`
  (offset 5148 of the 6482-byte golden file). valgrind: "Syscall param write(buf) points to
  uninitialised byte(s) ... Uninitialised value was created by a stack allocation at
  ArrowIpcEncoderEncodeFooter". Clang on macOS, and gcc with -O3, happened to write zeros.
- **Change:** each destination Block in the footer buffer is cleared with `memset` and then
  its three fields are assigned; no temporary struct is copied.
- **Effect on the bytes:** none where the padding was already zero (the golden files are unchanged and
  writer_scheme stays 1); elsewhere the padding is now always zero.
- **Reason it matters here:** the OID is a hash of the file bytes, so indeterminate bytes in the file make
  the OID of one run depend on stack contents.
- **Upstream:** not yet reported (the pinned commit is 41c78c476817d1d122acc59ddacb7a3cbd00644c).
