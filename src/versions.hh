#pragma once

// Versions of the interfaces this build implements.
//   contract_version: docs/SIDECAR.md
//   spec_version:     docs/SPEC.md
//   hits schema:      docs/HITS.md
constexpr int kContractVersion = 1;
constexpr const char* kSpecVersion = "1.0";
constexpr const char* kHitsSchemaVersion = "1.0";
constexpr const char* kTemplateLayeredCalorimeter = "layered_calorimeter";

// The vendored nanoarrow (third_party/nanoarrow/README.md). The version string
// itself comes from the library (NANOARROW_VERSION).
constexpr const char* kNanoarrowCommit = "41c78c476817d1d122acc59ddacb7a3cbd00644c";

// The scheme that turns hits into the bytes of hits.arrow: the nanoarrow code, how
// it is used (dictionary, batch size, end-of-stream marker) and the column layout.
// Bump it whenever the golden-file test (tests/test_hits_writer.cc) has to change;
// it is recorded in provenance.json (docs/HITS.md, section 6).
constexpr int kWriterScheme = 1;

// The scheme that derives per-event seeds (docs/HITS.md, section 4.2).
constexpr int kSeedingScheme = 1;
