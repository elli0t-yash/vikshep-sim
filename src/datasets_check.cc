#include "datasets_check.hh"

#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <filesystem>

namespace {

bool contains(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

}  // namespace

const char* process_getenv(const char* name)
{
  return std::getenv(name);
}

std::vector<DataSet> required_data_sets(const std::string& physics_list)
{
  // Data sets that every reference physics list reads.
  std::vector<DataSet> sets = {
      {"G4EMLOW", "G4LEDATA", "geant4-data-emlow"},
      {"G4PARTICLEXS", "G4PARTICLEXSDATA", "geant4-data-particlexs"},
      {"G4ENSDFSTATE", "G4ENSDFSTATEDATA", "geant4-data-ensdfstate"},
      {"PhotonEvaporation", "G4LEVELGAMMADATA", "geant4-data-photonevaporation"},
  };
  // Extra data sets for some families of lists.
  if (contains(physics_list, "HP") || contains(physics_list, "LEND")) {  // _HP, _AllHP
    sets.push_back({"G4NDL", "G4NEUTRONHPDATA", "geant4-data-ndl"});
  }
  if (contains(physics_list, "INCLXX")) {
    sets.push_back({"G4INCL", "G4INCLDATA", "geant4-data-incl"});
    sets.push_back({"G4ABLA", "G4ABLADATA", "geant4-data-abla"});
  }
  return sets;
}

std::vector<DataSet> find_missing_data_sets(const std::vector<DataSet>& required,
                                            const GetEnv& get_env)
{
  std::vector<DataSet> missing;
  for (const DataSet& set : required) {
    const char* value = get_env(set.env_var.c_str());
    std::error_code ec;
    if (value == nullptr || *value == '\0' || !std::filesystem::is_directory(value, ec)) {
      missing.push_back(set);
    }
  }
  return missing;
}

std::string describe_missing(const std::vector<DataSet>& missing)
{
  std::string text = "required Geant4 data sets not found:";
  std::string packages;
  for (const DataSet& set : missing) {
    text += "\n  " + set.name + " (environment variable " + set.env_var +
            " is unset or is not a directory)";
    packages += " " + set.conda_package;
  }
  text += "\nActivate the conda environment created from environment.yml, which sets these "
          "variables, or install the data sets into it with:\n  micromamba install -n <env> "
          "-c conda-forge" +
          packages;
  return text;
}

namespace {

// "geant4-data-emlow-8.8.0-hd8ed1ab_0.json" with package "geant4-data-emlow" gives
// version "8.8.0" and build "hd8ed1ab_0". Returns false if the name does not fit.
bool parse_conda_record_name(const std::string& file, const std::string& package,
                             std::string& version, std::string& build)
{
  const std::string suffix = ".json";
  if (file.size() <= suffix.size() ||
      file.compare(file.size() - suffix.size(), suffix.size(), suffix) != 0) {
    return false;
  }
  const std::string stem = file.substr(0, file.size() - suffix.size());
  if (stem.compare(0, package.size() + 1, package + "-") != 0) {
    return false;
  }
  const std::string rest = stem.substr(package.size() + 1);  // "<version>-<build>"
  const std::size_t dash = rest.rfind('-');
  if (dash == std::string::npos || dash == 0 || dash + 1 >= rest.size()) {
    return false;
  }
  version = rest.substr(0, dash);
  build = rest.substr(dash + 1);
  // The version starts with a digit: this keeps "geant4-data-pii" from matching a
  // package called "geant4-data-pii-extra".
  return std::isdigit(static_cast<unsigned char>(version[0])) != 0;
}

}  // namespace

std::vector<DataSetRecord> describe_data_sets(const std::vector<DataSet>& required, const GetEnv& get_env)
{
  namespace fs = std::filesystem;
  std::vector<DataSetRecord> records;
  for (const DataSet& set : required) {
    DataSetRecord record;
    record.env_var = set.env_var;
    record.directory = "unknown";
    record.conda_package = record.conda_version = record.conda_build = "unknown";

    const char* value = get_env(set.env_var.c_str());
    if (value != nullptr && *value != '\0') {
      fs::path dir(value);
      while (!dir.empty() && !dir.has_filename()) {
        dir = dir.parent_path();  // a trailing slash
      }
      record.directory = dir.filename().string();

      const fs::path data = dir.parent_path();      // .../share/Geant4/data
      const fs::path geant4 = data.parent_path();   // .../share/Geant4
      const fs::path share = geant4.parent_path();  // .../share
      if (data.filename() == "data" && geant4.filename() == "Geant4" && share.filename() == "share") {
        const fs::path meta = share.parent_path() / "conda-meta";
        std::error_code ec;
        std::vector<std::string> found;
        for (fs::directory_iterator it(meta, ec), end; !ec && it != end; it.increment(ec)) {
          std::string version, build;
          const std::string name = it->path().filename().string();
          if (parse_conda_record_name(name, set.conda_package, version, build)) {
            found.push_back(name);
          }
        }
        if (!found.empty()) {
          std::sort(found.begin(), found.end());
          std::string version, build;
          parse_conda_record_name(found.front(), set.conda_package, version, build);
          record.conda_package = set.conda_package;
          record.conda_version = version;
          record.conda_build = build;
        }
      }
    }
    records.push_back(record);
  }
  std::sort(records.begin(), records.end(),
            [](const DataSetRecord& a, const DataSetRecord& b) { return a.env_var < b.env_var; });
  return records;
}
