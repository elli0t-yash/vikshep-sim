#pragma once

#include <stdexcept>
#include <string>
#include <utility>

// Exceptions that map to stable error codes of docs/SIDECAR.md, section 4. Anything else
// that is thrown becomes INTERNAL (or SIMULATION_FAILED for a std::runtime_error from the
// simulation).

// Writing the output failed (the disk is full, a file cannot be created...): OUTPUT_NOT_WRITABLE.
class WriteError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// The geometry cannot be built: GEOMETRY_ERROR.
class GeometryError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// An internal check failed (for example the hash of the file on disk differs from the hash
// computed while writing it): INTERNAL.
class IntegrityError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// The 3-D export (geometry.glb, trajectories.json) failed: INTERNAL, whatever the cause
// (docs/EXPORT3D.md, section 5).
class ExportError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// A failure with a stable code chosen at the throw site (OUTPUT_EXISTS, DATASETS_MISSING,
// CANCELLED, ...).
class CodedError : public std::runtime_error {
 public:
  CodedError(std::string code, const std::string& message) : std::runtime_error(message), code_(std::move(code)) {}
  const std::string& code() const { return code_; }

 private:
  std::string code_;
};

// The exit code of a stable error code (docs/SIDECAR.md, section 3).
inline int exit_code_for(const std::string& code)
{
  if (code == "USAGE" || code == "SPEC_INVALID" || code == "SPEC_UNREADABLE" || code == "SPEC_NOT_JSON" ||
      code == "SPEC_VERSION_UNSUPPORTED" || code == "OUTPUT_EXISTS") {
    return 2;
  }
  if (code == "DATASETS_MISSING") {
    return 3;
  }
  if (code == "CANCELLED") {
    return 4;
  }
  return 1;  // GEOMETRY_ERROR, OUTPUT_NOT_WRITABLE, SIMULATION_FAILED, INTERNAL
}
