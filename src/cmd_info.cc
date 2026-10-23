#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "G4PhysListFactory.hh"
#include "G4Version.hh"
#include "build_config.hh"
#include "commands.hh"

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

std::vector<std::string> to_strings(const std::vector<G4String>& in)
{
  return std::vector<std::string>(in.begin(), in.end());
}

// nlohmann::json objects are std::map based, so keys serialize sorted.
nlohmann::json build_info()
{
  // The factory only lists names. It does not build a physics list and no
  // run manager is created, so no Geant4 data sets are needed.
  G4PhysListFactory factory;

  nlohmann::json j;
  j["tool_version"] = VIKSHEP_SIM_VERSION;
  j["geant4"] = {
      {"version", VIKSHEP_GEANT4_VERSION_STRING},
      {"version_number", G4VERSION_NUMBER},
      {"gdml", VIKSHEP_GEANT4_GDML != 0},
      {"multithreaded", VIKSHEP_GEANT4_MULTITHREADED != 0},
  };
  j["physics_lists"] = to_strings(factory.AvailablePhysLists());
  // The factory lists the default (no EM option) as an empty string. Omitting
  // the option selects the physics list's default, so it is not listed here.
  std::vector<std::string> em_options;
  for (const std::string& name : to_strings(factory.AvailablePhysListsEM())) {
    if (!name.empty()) {
      em_options.push_back(name);
    }
  }
  j["em_options"] = em_options;
  j["templates"] = nlohmann::json::array();
  j["datasets"] = nlohmann::json::array();
  j["platform"] = {
      {"os", os_name()},
      {"arch", arch_name()},
      {"compiler", compiler_name()},
  };
  return j;
}

void print_summary(const nlohmann::json& j)
{
  std::cout << "vikshep-sim " << j["tool_version"].get<std::string>() << "\n";
  const auto& g = j["geant4"];
  std::cout << "Geant4 " << g["version"].get<std::string>()
            << " (gdml: " << (g["gdml"].get<bool>() ? "yes" : "no")
            << ", multithreaded: " << (g["multithreaded"].get<bool>() ? "yes" : "no") << ")\n";
  std::cout << "physics lists: " << j["physics_lists"].size() << "\n";
  std::cout << "EM options: " << j["em_options"].size() << "\n";
  const auto& p = j["platform"];
  std::cout << "platform: " << p["os"].get<std::string>() << " " << p["arch"].get<std::string>()
            << ", " << p["compiler"].get<std::string>() << "\n";
}

}  // namespace

void register_info(CLI::App& app, int& exit_code)
{
  CLI::App* sub = app.add_subcommand("info", "Print tool and Geant4 build information");
  sub->footer("em_options lists suffixes that can be appended to a physics list name.\n"
              "Omitting the EM option selects the physics list's default.");
  auto as_json = std::make_shared<bool>(false);
  sub->add_flag("--json", *as_json, "Print one JSON object with sorted keys");
  sub->callback([as_json, &exit_code]() {
    const nlohmann::json j = build_info();
    if (*as_json) {
      std::cout << j.dump(2) << "\n";
    }
    else {
      print_summary(j);
    }
    exit_code = kExitOk;
  });
}
