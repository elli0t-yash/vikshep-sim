#pragma once

#include <functional>
#include <string>
#include <vector>

// Geant4 reads its physics data from directories named by environment
// variables. The conda-forge geant4 package depends on one conda package per
// data set (geant4-data-*); activating the environment sets the variables.

struct DataSet {
  std::string name;           // Geant4 name, for example "G4EMLOW"
  std::string env_var;        // for example "G4LEDATA"
  std::string conda_package;  // for example "geant4-data-emlow"
};

// The data sets a physics list needs.
std::vector<DataSet> required_data_sets(const std::string& physics_list);

// Looks up an environment variable; returns nullptr when unset. Replaceable
// for tests.
using GetEnv = std::function<const char*(const char*)>;
const char* process_getenv(const char* name);

// The required data sets whose variable is unset or does not name a directory.
std::vector<DataSet> find_missing_data_sets(const std::vector<DataSet>& required,
                                            const GetEnv& get_env = process_getenv);

// Text that says what is missing and how to install it.
std::string describe_missing(const std::vector<DataSet>& missing);
