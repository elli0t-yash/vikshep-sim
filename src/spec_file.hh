#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "spec_validate.hh"

// The error line of docs/SIDECAR.md, section 2.1. Keys are sorted.
nlohmann::json make_error_line(const std::string& code, const std::string& message,
                               const std::vector<SpecIssue>* issues = nullptr);

// Reads, parses and validates a spec file. On failure ok is false and
// error_line (SIDECAR.md error line) and human (text for stderr) are set; the
// exit code is always 2.
struct SpecLoad {
  bool ok = false;
  nlohmann::json spec;
  nlohmann::json error_line;
  std::string human;
};

SpecLoad load_valid_spec(const std::string& path);
