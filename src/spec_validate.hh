#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// One problem found in a spec. The pointer is an RFC 6901 JSON Pointer to the
// offending field ("" is the root object).
struct SpecIssue {
  std::string pointer;
  std::string message;
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

// Checks a parsed spec against docs/SPEC.md and reports every problem.
ValidationResult validate_spec(const nlohmann::json& spec, const Catalogue& catalogue);

// [{"message": ..., "pointer": ...}, ...]
nlohmann::json issues_to_json(const std::vector<SpecIssue>& issues);
