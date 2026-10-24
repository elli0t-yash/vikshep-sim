#pragma once

#include <string>

// What the current exception means for the sidecar contract (docs/SIDECAR.md, section 4):
// a stable code, a short message for stdout (no internals), and the detail for stderr.
struct Failure {
  std::string code;
  std::string message;
  std::string detail;
};

// Call inside a catch block. Mapping:
//   CodedError (CANCELLED, DATASETS_MISSING, OUTPUT_EXISTS, ...)  its own code
//   WriteError          OUTPUT_NOT_WRITABLE
//   GeometryError       GEOMETRY_ERROR
//   IntegrityError      INTERNAL
//   other std::runtime_error (from the simulation)  SIMULATION_FAILED
//   any other exception, including non-std ones     INTERNAL
Failure classify_current_exception();
