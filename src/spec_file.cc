#include "spec_file.hh"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "spec_catalogue.hh"

nlohmann::json make_error_line(const std::string& code, const std::string& message,
                               const std::vector<SpecIssue>* issues)
{
  nlohmann::json line = {{"code", code}, {"event", "error"}, {"message", message}};
  if (issues != nullptr) {
    line["errors"] = issues_to_json(*issues);
  }
  return line;
}

SpecLoad load_valid_spec(const std::string& path)
{
  SpecLoad load;

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    load.error_line = make_error_line("SPEC_UNREADABLE", "cannot read the spec file");
    load.human = "cannot read spec file: " + path;
    return load;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  try {
    load.spec = nlohmann::json::parse(text);
  }
  catch (const nlohmann::json::parse_error& e) {
    load.error_line = make_error_line("SPEC_NOT_JSON", "the spec file is not valid JSON");
    load.human = std::string("spec file is not valid JSON: ") + e.what();
    return load;
  }

  const Catalogue catalogue = load_geant4_catalogue();
  const ValidationResult result = validate_spec(load.spec, catalogue);
  if (result.ok()) {
    load.ok = true;
    return load;
  }

  std::string message;
  if (result.code == "SPEC_VERSION_UNSUPPORTED") {
    message = "spec_version is not supported";
  }
  else {
    const std::size_t n = result.issues.size();
    message = std::to_string(n) + (n == 1 ? " problem" : " problems") + " in spec";
  }
  for (const SpecIssue& issue : result.issues) {
    load.human += (issue.pointer.empty() ? "(root)" : issue.pointer) + ": " + issue.message + "\n";
  }
  load.human += message;
  load.error_line = make_error_line(result.code, message, &result.issues);
  return load;
}
