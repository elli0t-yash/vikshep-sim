#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "datasets_check.hh"

// provenance.json and run_summary.json (docs/SIDECAR.md, section 5).

struct ProvenanceFacts {
  std::string run_manager;        // for example "MT (G4MTRunManager)"
  double default_cut_mm = 0.0;    // default production cut in effect
  std::vector<DataSetRecord> data_sets;
  CondaPackageRecord geant4_conda;  // the conda package geant4: version and build string
};

// provenance.json: what produced the hits. Deterministic: the same build, platform, spec
// and seed give the same bytes. It holds no timestamp, no absolute path, no host or
// user name and no thread count.
//
// resolved_spec: the output of resolve_spec(). Keys are sorted (nlohmann::json objects
// are ordered maps).
nlohmann::json make_provenance(const nlohmann::json& resolved_spec, const ProvenanceFacts& facts);

struct RunSummaryFacts {
  std::string oid;
  std::uint64_t events_requested = 0;
  std::uint64_t events_completed = 0;
  std::uint64_t events_with_hits = 0;
  std::uint64_t events_without_hits = 0;
  std::uint64_t hits_total = 0;
  std::uint64_t hits_file_bytes = 0;
  int threads = 0;          // not reproducible: summary only
  double wall_time_s = 0.0; // not reproducible: summary only
};

// run_summary.json. Contains wall time and the thread count, so it is not reproducible
// byte for byte, and it is not hashed.
nlohmann::json make_run_summary(const RunSummaryFacts& facts);

// The text written to a .json output file: two-space indentation and a final newline.
std::string to_file_text(const nlohmann::json& document);
