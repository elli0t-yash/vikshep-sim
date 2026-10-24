#include "failure.hh"

#include <exception>
#include <stdexcept>

#include "errors.hh"

Failure classify_current_exception()
{
  try {
    throw;
  }
  catch (const CodedError& e) {
    return {e.code(), e.code() == "CANCELLED" ? "the run was cancelled" : e.what(), e.what()};
  }
  catch (const WriteError& e) {
    return {"OUTPUT_NOT_WRITABLE", "writing the output failed", e.what()};
  }
  catch (const GeometryError& e) {
    return {"GEOMETRY_ERROR", "the geometry cannot be built", e.what()};
  }
  catch (const IntegrityError& e) {
    return {"INTERNAL", "an internal check failed", e.what()};
  }
  catch (const std::runtime_error& e) {
    return {"SIMULATION_FAILED", "the simulation failed", e.what()};
  }
  catch (const std::exception& e) {
    return {"INTERNAL", "internal error", e.what()};
  }
  catch (...) {
    return {"INTERNAL", "internal error", "unknown exception"};
  }
}
