#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "datasets_check.hh"

namespace {

bool has(const std::vector<DataSet>& sets, const std::string& env_var)
{
  return std::any_of(sets.begin(), sets.end(),
                     [&](const DataSet& s) { return s.env_var == env_var; });
}

}  // namespace

TEST_CASE("every reference list needs the core data sets", "[datasets]")
{
  for (const char* list : {"FTFP_BERT", "QBBC", "QGSP_BIC"}) {
    const auto sets = required_data_sets(list);
    CHECK(has(sets, "G4LEDATA"));
    CHECK(has(sets, "G4PARTICLEXSDATA"));
    CHECK(has(sets, "G4ENSDFSTATEDATA"));
    CHECK(has(sets, "G4LEVELGAMMADATA"));
    CHECK_FALSE(has(sets, "G4NEUTRONHPDATA"));
    CHECK_FALSE(has(sets, "G4INCLDATA"));
  }
}

TEST_CASE("some list families need more", "[datasets]")
{
  CHECK(has(required_data_sets("FTFP_BERT_HP"), "G4NEUTRONHPDATA"));
  CHECK(has(required_data_sets("QGSP_BIC_AllHP"), "G4NEUTRONHPDATA"));
  CHECK(has(required_data_sets("FTFP_INCLXX"), "G4INCLDATA"));
  CHECK(has(required_data_sets("FTFP_INCLXX"), "G4ABLADATA"));
}

TEST_CASE("missing data sets are found", "[datasets]")
{
  const auto required = required_data_sets("FTFP_BERT");
  const std::string existing = std::filesystem::temp_directory_path().string();

  SECTION("all present")
  {
    const GetEnv env = [&](const char*) { return existing.c_str(); };
    CHECK(find_missing_data_sets(required, env).empty());
  }
  SECTION("unset variable")
  {
    const GetEnv env = [&](const char* name) -> const char* {
      return std::string(name) == "G4LEDATA" ? nullptr : existing.c_str();
    };
    const auto missing = find_missing_data_sets(required, env);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].env_var == "G4LEDATA");
  }
  SECTION("variable names a directory that does not exist")
  {
    const std::string gone = existing + "/vikshep-no-such-directory";
    const GetEnv env = [&](const char* name) -> const char* {
      return std::string(name) == "G4PARTICLEXSDATA" ? gone.c_str() : existing.c_str();
    };
    const auto missing = find_missing_data_sets(required, env);
    REQUIRE(missing.size() == 1);
    CHECK(missing[0].env_var == "G4PARTICLEXSDATA");
  }
  SECTION("empty value")
  {
    const GetEnv env = [&](const char*) { return ""; };
    CHECK(find_missing_data_sets(required, env).size() == required.size());
  }
}

TEST_CASE("the message says what to install", "[datasets]")
{
  const std::string text = describe_missing({{"G4EMLOW", "G4LEDATA", "geant4-data-emlow"}});
  CHECK(text.find("G4EMLOW") != std::string::npos);
  CHECK(text.find("G4LEDATA") != std::string::npos);
  CHECK(text.find("micromamba install -n <env> -c conda-forge geant4-data-emlow") !=
        std::string::npos);
}
