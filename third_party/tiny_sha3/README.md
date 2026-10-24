# Vendored tiny_sha3

SHA3 / Keccak in a few lines of C, used to compute the OID of `hits.arrow`
(docs/HITS.md, section 6): SHA3-256, first 14 bytes.

| Item | Value |
|------|-------|
| Project | tiny_sha3 by Markku-Juhani O. Saarinen, https://github.com/mjosaarinen/tiny_sha3 |
| Commit | `dcbb3192047c2a721f5f851db591871d428036a9` (2016-09-21) |
| Files | `sha3.c`, `sha3.h` (unmodified); sha256 `0c08d2e5945048d7af5b910678a85908b4b6ec7d4cb05446d807b1389d71dba5` and `d6801d7235145d2c0dfa8f1f2b88a41b62ca393f409fdf87b836638ec89ebb67` |
| Licence | MIT (`LICENSE.txt`), Copyright (c) 2015 Markku-Juhani O. Saarinen |

It is checked against the official SHA3-256 test vectors (empty string, "abc",
the 448-bit message of FIPS 202, one million times "a") in tests/test_sha3.cc.
