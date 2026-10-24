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

// What provenance.json records about one data set (docs/HITS.md, section 6).
struct DataSetRecord {
  std::string env_var;        // for example "G4LEDATA"
  std::string directory;      // base name of the data directory, for example "EMLOW8.8"
  std::string conda_package;  // for example "geant4-data-emlow", or "unknown"
  std::string conda_version;  // for example "8.8.0", or "unknown"
  std::string conda_build;    // for example "hd8ed1ab_0", or "unknown"
};

// One record per required data set, sorted by environment variable name.
//
// The directory name carries the data set version (EMLOW8.8) and is what Geant4 reads.
// The conda record is looked up as follows: the data directory is
// <prefix>/share/Geant4/data/<name>, so <prefix> is four levels up; the conda metadata
// of the package is <prefix>/conda-meta/<package>-<version>-<build>.json, and the
// version and build are read from that file NAME (the file is not opened). If the
// directory is not laid out like that or no such file exists, the three conda fields
// are "unknown"; that is not an error.
std::vector<DataSetRecord> describe_data_sets(const std::vector<DataSet>& required,
                                              const GetEnv& get_env = process_getenv);

// What provenance.json records about the conda package `geant4` itself (key geant4_conda).
struct CondaPackageRecord {
  std::string version = "unknown";  // for example "11.4.3"
  std::string build = "unknown";    // for example "noqt_429d0ab_0"
};

// Same method as for the data sets: the environment prefix is found from the data directories
// (<prefix>/share/Geant4/data/<name>, four levels up), then <prefix>/conda-meta is searched for
// geant4-<version>-<build>.json and the version and build are read from the file name. A package
// of another name that starts with "geant4-" (geant4-data-*, geant4-qt-mutex) does not match,
// because the version must start with a digit. "unknown" if there is no such file; never an error.
CondaPackageRecord describe_geant4_package(const std::vector<DataSet>& required,
                                           const GetEnv& get_env = process_getenv);
