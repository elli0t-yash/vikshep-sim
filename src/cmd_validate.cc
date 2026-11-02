#include <iostream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "commands.hh"
#include "spec_canonical.hh"
#include "spec_file.hh"
#include "versions.hh"

namespace {

int run_validate(const std::string& path, bool canonical)
{
  const SpecLoad load = load_valid_spec(path);
  if (!load.ok) {
    std::cout << load.error_line.dump() << "\n";
    std::cerr << load.human << "\n";
    return kExitUsageOrSpec;
  }

  if (canonical) {
    std::cout << canonical_spec(load.spec) << "\n";
  }
  else {
    // The spec's own version: a spec written as 1.0 is reported as 1.0.
    std::cout << nlohmann::json({{"event", "valid"}, {"spec_version", load.spec.at("spec_version")}}).dump()
              << "\n";
  }
  return kExitOk;
}

}  // namespace

void register_validate(CLI::App& app, int& exit_code)
{
  CLI::App* sub = app.add_subcommand("validate", "Check a spec file without running a simulation");
  auto path = std::make_shared<std::string>();
  auto canonical = std::make_shared<bool>(false);
  sub->add_option("--spec", *path, "Path to the JSON spec")->required();
  sub->add_flag("--canonical", *canonical, "Print the canonical form of a valid spec");
  sub->footer(
      "Valid: exit 0 and one JSON line {\"event\":\"valid\",...} (or the canonical spec).\n"
      "Invalid, unreadable or not JSON: exit 2 and one JSON error line on stdout\n"
      "(codes SPEC_INVALID, SPEC_VERSION_UNSUPPORTED, SPEC_UNREADABLE, SPEC_NOT_JSON).\n"
      "Human-readable text goes to stderr.");
  sub->callback([path, canonical, &exit_code]() { exit_code = run_validate(*path, *canonical); });
}
