#pragma once

// What a Geant4 test does when the Geant4 data sets are missing: skip, with a message, or, if the
// environment variable VIKSHEP_REQUIRE_DATA is set (CI sets it), fail, so that CI can never go green
// by skipping. Python tests do the same (tests/vikshep_testlib.py).

#include <cstdlib>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "datasets_check.hh"

inline bool require_data_sets()
{
  const char* value = std::getenv("VIKSHEP_REQUIRE_DATA");
  return value != nullptr && *value != '\0' && std::string(value) != "0";
}

#define SKIP_WITHOUT_DATA_SETS()                                                                         \
  do {                                                                                                   \
    const auto vikshep_missing = find_missing_data_sets(required_data_sets("FTFP_BERT"));                \
    if (!vikshep_missing.empty()) {                                                                      \
      if (require_data_sets()) {                                                                         \
        FAIL("VIKSHEP_REQUIRE_DATA is set but the Geant4 data sets are missing: "                        \
             << describe_missing(vikshep_missing));                                                      \
      }                                                                                                  \
      SKIP("SKIPPED, not passed: Geant4 data sets are missing. " << describe_missing(vikshep_missing)); \
    }                                                                                                    \
  } while (0)
