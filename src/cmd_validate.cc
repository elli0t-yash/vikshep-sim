#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "commands.hh"
#include "spec_canonical.hh"
#include "spec_catalogue.hh"
#include "spec_validate.hh"
#include "versions.hh"

namespace {

// The error line of docs/SIDECAR.md, section 2.1. Keys are sorted.
nlohmann::json error_line(const std::string& code, const std::string& message,
                          const std::vector<SpecIssue>* issues = nullptr)
{
  nlohmann::json line = {{"code", code}, {"event", "error"}, {"message", message}};
  if (issues != nullptr) {
    line["errors"] = issues_to_json(*issues);
  }
  return line;
}

int fail(const nlohmann::json& line, const std::string& human)
{
  std::cout << line.dump() << "\n";
  std::cerr << human << "\n";
  return kExitUsageOrSpec;
}

int run_validate(const std::string& path, bool canonical)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return fail(error_line("SPEC_UNREADABLE", "cannot read the spec file"),
                "cannot read spec file: " + path);
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  nlohmann::json spec;
  try {
    spec = nlohmann::json::parse(text);
  }
  catch (const nlohmann::json::parse_error& e) {
    return fail(error_line("SPEC_NOT_JSON", "the spec file is not valid JSON"),
                std::string("spec file is not valid JSON: ") + e.what());
  }

  const Catalogue catalogue = load_geant4_catalogue();
  const ValidationResult result = validate_spec(spec, catalogue);
  if (!result.ok()) {
    std::string message;
    if (result.code == "SPEC_VERSION_UNSUPPORTED") {
      message = "spec_version is not supported";
    }
    else {
      const std::size_t n = result.issues.size();
      message = std::to_string(n) + (n == 1 ? " problem" : " problems") + " in spec";
    }
    std::string human;
    for (const SpecIssue& issue : result.issues) {
      human += (issue.pointer.empty() ? "(root)" : issue.pointer) + ": " + issue.message + "\n";
    }
    human += message;
    return fail(error_line(result.code, message, &result.issues), human);
  }

  if (canonical) {
    std::cout << canonical_spec(spec) << "\n";
  }
  else {
    std::cout << nlohmann::json({{"event", "valid"}, {"spec_version", kSpecVersion}}).dump()
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
