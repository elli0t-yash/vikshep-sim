#include "build_info.hh"

#include <string>

#include "G4Version.hh"
#include "build_config.hh"

namespace {

std::string os_name()
{
#if defined(__linux__)
  return "linux";
#elif defined(__APPLE__)
  return "macos";
#elif defined(_WIN32)
  return "windows";
#else
  return "unknown";
#endif
}

std::string arch_name()
{
#if defined(__aarch64__) || defined(__arm64__)
  return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#else
  return "unknown";
#endif
}

std::string compiler_name()
{
#if defined(__clang__)
  return "clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) +
         "." + std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
  return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
         std::to_string(__GNUC_PATCHLEVEL__);
#else
  return "unknown";
#endif
}

}  // namespace

nlohmann::json platform_json()
{
  return {
      {"os", os_name()},
      {"arch", arch_name()},
      {"compiler", compiler_name()},
  };
}

nlohmann::json geant4_json()
{
  return {
      {"version", VIKSHEP_GEANT4_VERSION_STRING},
      {"version_number", G4VERSION_NUMBER},
      {"gdml", VIKSHEP_GEANT4_GDML != 0},
      {"multithreaded", VIKSHEP_GEANT4_MULTITHREADED != 0},
  };
}
