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
  // The `export_3d` object of provenance.json (docs/SIDECAR.md, 5.1): null (the key is then absent)
  // unless the run used --export-3d.
  nlohmann::json export_3d;
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
  std::string spec_version;  // the spec's own version, as written in the spec file
  std::uint64_t events_requested = 0;
  std::uint64_t events_completed = 0;
  std::uint64_t events_with_hits = 0;
  std::uint64_t events_without_hits = 0;
  std::uint64_t hits_total = 0;
  std::uint64_t hits_file_bytes = 0;
  int threads = 0;          // not reproducible: summary only
  double wall_time_s = 0.0; // not reproducible: summary only
  // The `exports` array of run_summary.json: null (the key is then absent) unless the run used --export-3d.
  nlohmann::json exports;
};

// run_summary.json. Contains wall time and the thread count, so it is not reproducible
// byte for byte, and it is not hashed.
nlohmann::json make_run_summary(const RunSummaryFacts& facts);

// The text written to a .json output file: two-space indentation and a final newline.
std::string to_file_text(const nlohmann::json& document);
