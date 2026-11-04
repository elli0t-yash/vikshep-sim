#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "calorimeter_geometry.hh"
#include "tracker_geometry.hh"

// One problem found in a spec. The pointer is an RFC 6901 JSON Pointer to the
// offending field ("" is the root object).
struct SpecIssue {
  std::string pointer;
  std::string message;
  // For a missing required field: the name of the field. The pointer then
  // names the parent object. Empty otherwise.
  std::string missing;
};

// Names that depend on the installed Geant4. Filled by load_geant4_catalogue()
// (spec_catalogue.cc), so the validator itself needs no Geant4.
struct Catalogue {
  std::vector<std::string> materials;     // NIST material names
  std::vector<std::string> physics_lists; // reference physics list names
  std::vector<std::string> em_options;    // EM option suffixes
};

// Result of validation. code is empty when the spec is valid, otherwise a
// stable code from docs/SIDECAR.md: SPEC_INVALID or SPEC_VERSION_UNSUPPORTED.
struct ValidationResult {
  std::string code;
  std::vector<SpecIssue> issues;
  bool ok() const { return code.empty(); }
};

// Particles accepted by gun.particle.
const std::vector<std::string>& allowed_particles();

// The geometry numbers of a spec, if all of them are present and valid.
bool read_geometry(const nlohmann::json& spec, CalorimeterGeometry& out);

// The tracker_barrel layers of a spec, if every layer has valid numbers (ordering of the
// layers is not checked here). The material is filled in with its default when absent.
bool read_tracker_geometry(const nlohmann::json& spec, TrackerGeometry& out);

// True if spec_version is one this build accepts.
bool is_supported_spec_version(const std::string& version);

// Checks a parsed spec against docs/SPEC.md and reports every problem.
ValidationResult validate_spec(const nlohmann::json& spec, const Catalogue& catalogue);

// [{"message": ..., "missing": ... (only if set), "pointer": ...}, ...]
nlohmann::json issues_to_json(const std::vector<SpecIssue>& issues);
