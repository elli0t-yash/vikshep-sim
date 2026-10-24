#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "build_config.hh"
#include "cli_helpers.hh"
#include "data_sets_guard.hh"
#include "datasets_check.hh"
#include "run_outputs.hh"
#include "sha3_256.hh"
#include "spec_canonical.hh"
#include "versions.hh"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

json example_resolved()
{
  std::ifstream in(cli::example_path());
  return resolve_spec(json::parse(in));
}

// A directory tree that looks like a conda environment with two data sets.
class FakeEnvironment {
 public:
  FakeEnvironment()
  {
    static int counter = 0;
    root_ = fs::temp_directory_path() /
            ("vikshep-fake-env-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    fs::create_directories(root_ / "share" / "Geant4" / "data" / "EMLOW8.8");
    fs::create_directories(root_ / "share" / "Geant4" / "data" / "PARTICLEXS4.2");
    fs::create_directories(root_ / "conda-meta");
    std::ofstream(root_ / "conda-meta" / "geant4-data-emlow-8.8.0-hd8ed1ab_0.json") << "{}";
    std::ofstream(root_ / "conda-meta" / "geant4-data-particlexs-4.2.0-hd8ed1ab_2.json") << "{}";
    std::ofstream(root_ / "conda-meta" / "geant4-data-particlexs-extra-9.9-h0_0.json") << "{}";
  }
  ~FakeEnvironment()
  {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  fs::path root() const { return root_; }

 private:
  fs::path root_;
};

}  // namespace

TEST_CASE("data set versions are read from the conda metadata file names", "[provenance]")
{
  FakeEnvironment env;
  const std::string emlow = (env.root() / "share/Geant4/data/EMLOW8.8").string();
  const std::string xs = (env.root() / "share/Geant4/data/PARTICLEXS4.2").string() + "/";  // trailing slash
  const GetEnv get = [&](const char* name) -> const char* {
    const std::string n = name;
    if (n == "G4LEDATA") return emlow.c_str();
    if (n == "G4PARTICLEXSDATA") return xs.c_str();
    return nullptr;
  };
  const auto required = std::vector<DataSet>{{"G4PARTICLEXS", "G4PARTICLEXSDATA", "geant4-data-particlexs"},
                                             {"G4EMLOW", "G4LEDATA", "geant4-data-emlow"}};
  const auto records = describe_data_sets(required, get);
  REQUIRE(records.size() == 2);
  // sorted by variable name
  CHECK(records[0].env_var == "G4LEDATA");
  CHECK(records[0].directory == "EMLOW8.8");
  CHECK(records[0].conda_package == "geant4-data-emlow");
  CHECK(records[0].conda_version == "8.8.0");
  CHECK(records[0].conda_build == "hd8ed1ab_0");
  CHECK(records[1].env_var == "G4PARTICLEXSDATA");
  CHECK(records[1].directory == "PARTICLEXS4.2");
  CHECK(records[1].conda_version == "4.2.0");   // not the "-extra" package
  CHECK(records[1].conda_build == "hd8ed1ab_2");
}

TEST_CASE("unknown conda metadata is recorded as unknown and is not an error", "[provenance]")
{
  FakeEnvironment env;
  fs::remove_all(env.root() / "conda-meta");
  const std::string emlow = (env.root() / "share/Geant4/data/EMLOW8.8").string();
  const std::string elsewhere = (env.root() / "somewhere" / "EMLOW8.8").string();
  fs::create_directories(elsewhere);
  const GetEnv get = [&](const char* name) -> const char* {
    return std::string(name) == "G4LEDATA" ? emlow.c_str() : elsewhere.c_str();
  };
  const auto required = std::vector<DataSet>{{"G4EMLOW", "G4LEDATA", "geant4-data-emlow"},
                                             {"G4PARTICLEXS", "G4PARTICLEXSDATA", "geant4-data-particlexs"}};
  for (const DataSetRecord& r : describe_data_sets(required, get)) {
    CHECK(r.directory != "unknown");  // the directory name is always known
    CHECK(r.conda_package == "unknown");
    CHECK(r.conda_version == "unknown");
    CHECK(r.conda_build == "unknown");
  }
  const GetEnv nothing = [](const char*) -> const char* { return nullptr; };
  for (const DataSetRecord& r : describe_data_sets(required, nothing)) {
    CHECK(r.directory == "unknown");
  }
}

TEST_CASE("the data sets of this environment are found when the environment is active", "[provenance]")
{
  SKIP_WITHOUT_DATA_SETS();
  const auto required = required_data_sets("FTFP_BERT");
  for (const DataSetRecord& r : describe_data_sets(required)) {
    INFO(r.env_var << " " << r.directory);
    CHECK(r.directory != "unknown");
    CHECK(r.directory.find('/') == std::string::npos);
  }
}

namespace {

ProvenanceFacts facts()
{
  ProvenanceFacts f;
  f.run_manager = "MT (G4MTRunManager)";
  f.default_cut_mm = 0.7;
  f.data_sets = {{"G4LEDATA", "EMLOW8.8", "geant4-data-emlow", "8.8.0", "hd8ed1ab_0"}};
  return f;
}

void collect_strings(const json& j, std::vector<std::string>& out)
{
  if (j.is_string()) out.push_back(j.get<std::string>());
  else if (j.is_array() || j.is_object()) for (const auto& v : j) collect_strings(v, out);
}

}  // namespace

TEST_CASE("provenance.json has the guaranteed keys and the new ones", "[provenance]")
{
  const json p = make_provenance(example_resolved(), facts());
  std::set<std::string> keys;
  for (auto it = p.begin(); it != p.end(); ++it) keys.insert(it.key());
  const std::set<std::string> expected = {
      "build_type", "contract_version", "data_sets", "default_production_cut_mm", "geant4", "hits_schema_version",
      "nanoarrow", "physics_list", "platform", "run_manager", "seed", "seeding_scheme", "spec",
      "spec_oid", "spec_version", "tool_version", "writer_scheme"};
  CHECK(keys == expected);
  CHECK(p.at("build_type") == VIKSHEP_BUILD_TYPE);
  CHECK(p.at("seeding_scheme") == 1);
  CHECK(p.at("writer_scheme") == 1);
  CHECK(p.at("seed") == 42);
  CHECK(p.at("nanoarrow").at("version") == "0.10.0-SNAPSHOT");
  CHECK(p.at("nanoarrow").at("commit") == "41c78c476817d1d122acc59ddacb7a3cbd00644c");
  CHECK(p.at("physics_list") == "FTFP_BERT");
  CHECK(p.at("default_production_cut_mm") == 0.7);
  CHECK(p.at("spec") == example_resolved());
  CHECK(p.at("geant4").contains("version"));
  CHECK(p.at("platform").contains("compiler"));
  REQUIRE(p.at("data_sets").size() == 1);
  CHECK(p.at("data_sets")[0].at("conda_version") == "8.8.0");
}

TEST_CASE("spec_oid is the OID of the canonical spec", "[provenance]")
{
  const json spec = example_resolved();
  const json p = make_provenance(spec, facts());
  CHECK(p.at("spec_oid") == oid_of_bytes(canonical_spec(spec)));
  CHECK(p.at("spec_oid").get<std::string>().size() == 28);
}

TEST_CASE("provenance.json is deterministic and holds nothing run-specific", "[provenance]")
{
  const std::string a = to_file_text(make_provenance(example_resolved(), facts()));
  const std::string b = to_file_text(make_provenance(example_resolved(), facts()));
  CHECK(a == b);
  CHECK(a.back() == '\n');
  // no thread count, no wall time, no timestamp, no absolute path
  // ("multithreaded" is a fact about the Geant4 build, not the thread count of the run.)
  for (const char* word : {"\"threads\"", "thread_count", "n_threads", "wall_time", "timestamp",
                           "\"date\"", "hostname", "\"pid\""}) {
    INFO(word);
    CHECK(a.find(word) == std::string::npos);
  }
  std::vector<std::string> strings;
  collect_strings(make_provenance(example_resolved(), facts()), strings);
  for (const std::string& s : strings) {
    INFO(s);
    CHECK(s.rfind('/', 0) != 0);  // does not start with a slash
    CHECK(s.find("/Users/") == std::string::npos);
    CHECK(s.find("/home/") == std::string::npos);
  }
}

TEST_CASE("the spec in provenance does not depend on how the spec file was written", "[provenance]")
{
  json reordered = json::parse(R"({"seed":42,"events":20,"gun":{"particle":"e-","energy_GeV":5.0},
    "geometry":{"transverse_mm":100,"active":{"thickness_mm":5,"material":"G4_lAr"},
    "absorber":{"thickness_mm":10,"material":"G4_Pb"},"layers":10},
    "template":"layered_calorimeter","spec_version":"1.0"})");
  CHECK(to_file_text(make_provenance(resolve_spec(reordered), facts())) ==
        to_file_text(make_provenance(example_resolved(), facts())));
}

TEST_CASE("run_summary.json has the keys of SIDECAR.md", "[summary]")
{
  RunSummaryFacts f;
  f.oid = "0123456789abcdef0123456789ab";
  f.events_requested = 20;
  f.events_completed = 20;
  f.events_with_hits = 18;
  f.events_without_hits = 2;
  f.hits_total = 1234;
  f.hits_file_bytes = 99999;
  f.threads = 4;
  f.wall_time_s = 1.5;
  const json s = make_run_summary(f);
  std::set<std::string> keys;
  for (auto it = s.begin(); it != s.end(); ++it) keys.insert(it.key());
  const std::set<std::string> expected = {
      "contract_version", "events_completed", "events_requested", "events_with_hits",
      "events_without_hits", "hits_file_bytes", "hits_schema_version", "hits_total", "oid",
      "spec_version", "threads", "wall_time_s"};
  CHECK(keys == expected);
  CHECK(s.at("oid") == f.oid);
  CHECK(s.at("hits_schema_version") == "1.0");
  CHECK(s.at("spec_version") == "1.0");
  CHECK(s.at("contract_version") == 1);
}
