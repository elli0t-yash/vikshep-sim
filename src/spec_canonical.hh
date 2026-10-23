#pragma once

#include <string>

#include <nlohmann/json.hpp>

// Both functions require a spec that passed validate_spec().

// The spec with every default written out, gun.direction normalised to length
// 1, sensitive sorted, and integer fields stored as integers.
nlohmann::json resolve_spec(const nlohmann::json& valid_spec);

// resolve_spec() serialised to bytes: sorted keys, no whitespace, numbers in
// the ECMAScript format (RFC 8785). No trailing newline.
std::string canonical_spec(const nlohmann::json& valid_spec);
