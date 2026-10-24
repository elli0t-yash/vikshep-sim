#include "datasets_check.hh"

#include <cstdlib>
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
