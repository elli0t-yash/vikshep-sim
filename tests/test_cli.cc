// Exit codes and stdout shape of the vikshep-sim executable (POSIX only).

#include <sys/wait.h>

#include <cstdio>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cli_helpers.hh"
#include "datasets_check.hh"
#include "spec_canonical.hh"

using nlohmann::json;

using cli::example_path;
using cli::line_count;
using cli::Outcome;
using cli::run;
using TempSpec = cli::TempFile;

TEST_CASE("validate: a good spec", "[cli]")
{
  const Outcome r = run("validate --spec \"" + example_path() + "\"");
  CHECK(r.code == 0);
  CHECK(r.out == "{\"event\":\"valid\",\"spec_version\":\"1.0\"}\n");
}

TEST_CASE("validate --canonical prints the canonical spec", "[cli]")
{
  const Outcome r = run("validate --canonical --spec \"" + example_path() + "\"");
  CHECK(r.code == 0);
  std::ifstream in(example_path());
  CHECK(r.out == canonical_spec(json::parse(in)) + "\n");
  CHECK(line_count(r.out) == 1);
}

TEST_CASE("validate: an invalid spec gives one SPEC_INVALID line and exit 2", "[cli]")
{
  const TempSpec spec(R"({"spec_version":"1.0","template":"layered_calorimeter",
    "geometry":{"layers":0,"absorber":{"material":"G4_Pbb","thickness_mm":10},
                "active":{"material":"G4_lAr","thickness_mm":5},"transverse_mm":100},
    "gun":{"particle":"e-","energy_GeV":5},"events":20,"seed":42,"extra":1})");
  const Outcome r = run("validate --spec \"" + spec.path() + "\"");
  CHECK(r.code == 2);
  CHECK(line_count(r.out) == 1);
  const json line = json::parse(r.out);
  CHECK(line.at("event") == "error");
  CHECK(line.at("code") == "SPEC_INVALID");
  CHECK(line.at("message") == "3 problems in spec");
  REQUIRE(line.at("errors").size() == 3);
  CHECK(line.at("errors")[0].at("pointer") == "/geometry/layers");
  CHECK_FALSE(line.at("errors")[0].contains("missing"));
  CHECK(line.at("errors")[1].at("pointer") == "/geometry/absorber/material");
  CHECK(line.at("errors")[2].at("pointer") == "/extra");
  // Keys are sorted: the line equals its own sorted serialisation.
  CHECK(r.out == line.dump() + "\n");
}

TEST_CASE("validate: a missing field gives the parent pointer and a missing key", "[cli]")
{
  const TempSpec spec(R"({"spec_version":"1.0","template":"layered_calorimeter",
    "geometry":{"layers":3,"absorber":{"material":"G4_Pb"},
                "active":{"material":"G4_lAr","thickness_mm":5},"transverse_mm":100},
    "gun":{"particle":"e-","energy_GeV":5},"events":20,"seed":42})");
  const Outcome r = run("validate --spec \"" + spec.path() + "\"");
  CHECK(r.code == 2);
  const json line = json::parse(r.out);
  REQUIRE(line.at("errors").size() == 1);
  CHECK(line.at("errors")[0].at("pointer") == "/geometry/absorber");
  CHECK(line.at("errors")[0].at("missing") == "thickness_mm");
  CHECK(r.out == line.dump() + "\n");
}

TEST_CASE("validate: a gun start outside the world", "[cli]")
{
  const TempSpec spec(R"({"spec_version":"1.0","template":"layered_calorimeter",
    "geometry":{"layers":3,"absorber":{"material":"G4_Pb","thickness_mm":10},
                "active":{"material":"G4_lAr","thickness_mm":5},"transverse_mm":100},
    "gun":{"particle":"e-","energy_GeV":5,"position_mm":[0,0,-500]},"events":20,"seed":42})");
  const Outcome r = run("validate --spec \"" + spec.path() + "\"");
  CHECK(r.code == 2);
  CHECK(json::parse(r.out).at("errors")[0].at("pointer") == "/gun/position_mm");
}

TEST_CASE("validate: unsupported spec_version", "[cli]")
{
  const TempSpec spec(R"({"spec_version":"9.9"})");
  const Outcome r = run("validate --spec \"" + spec.path() + "\"");
  CHECK(r.code == 2);
  CHECK(json::parse(r.out).at("code") == "SPEC_VERSION_UNSUPPORTED");
}

TEST_CASE("validate: unreadable file", "[cli]")
{
  const Outcome r = run("validate --spec /nonexistent/vikshep/spec.json");
  CHECK(r.code == 2);
  CHECK(line_count(r.out) == 1);
  CHECK(json::parse(r.out).at("code") == "SPEC_UNREADABLE");
}

TEST_CASE("validate: not JSON", "[cli]")
{
  const TempSpec spec("{ this is not json");
  const Outcome r = run("validate --spec \"" + spec.path() + "\"");
  CHECK(r.code == 2);
  CHECK(line_count(r.out) == 1);
  CHECK(json::parse(r.out).at("code") == "SPEC_NOT_JSON");
}

TEST_CASE("validate: root is not an object", "[cli]")
{
  const TempSpec spec("[1, 2, 3]");
  const Outcome r = run("validate --spec \"" + spec.path() + "\"");
  CHECK(r.code == 2);
  const json line = json::parse(r.out);
  CHECK(line.at("code") == "SPEC_INVALID");
  CHECK(line.at("errors")[0].at("pointer") == "");
}

TEST_CASE("usage errors exit 2 with nothing on stdout", "[cli]")
{
  for (const char* args : {"validate", "validate --nope", "nonsense", ""}) {
    DYNAMIC_SECTION("args: " << args)
    {
      const Outcome r = run(args);
      CHECK(r.code == 2);
      CHECK(r.out.empty());
    }
  }
}

TEST_CASE("--help exits 0", "[cli]")
{
  CHECK(run("--help").code == 0);
  CHECK(run("validate --help").code == 0);
}

TEST_CASE("run and datasets are stubs that exit 2", "[cli]")
{
  for (const char* sub : {"run", "datasets"}) {
    const Outcome r = run(sub);
    CHECK(r.code == 2);
    CHECK(r.out.empty());
  }
}

TEST_CASE("info --json lists the template and versions", "[cli]")
{
  const Outcome r = run("info --json");
  CHECK(r.code == 0);
  const json info = json::parse(r.out);
  CHECK(info.at("templates") == json::array({"layered_calorimeter"}));
  CHECK(info.at("spec_versions") == json::array({"1.0"}));
  CHECK(info.at("hits_schema_versions") == json::array({"1.0"}));
  CHECK(info.at("contract_version") == 1);
  // Sorted keys, no extra whitespace beyond the indented dump, byte-stable.
  CHECK(r.out == info.dump(2) + "\n");
  CHECK(run("info --json").out == r.out);
}

TEST_CASE("dev-run prints the table on stdout and nothing else", "[cli][geant4]")
{
  if (!find_missing_data_sets(required_data_sets("FTFP_BERT")).empty()) {
    SKIP("SKIPPED, not passed: Geant4 data sets are missing in this environment");
  }
  const TempSpec spec(R"({"spec_version":"1.0","template":"layered_calorimeter",
    "geometry":{"layers":3,"absorber":{"material":"G4_Pb","thickness_mm":10},
                "active":{"material":"G4_lAr","thickness_mm":5},"transverse_mm":100},
    "gun":{"particle":"e-","energy_GeV":0.5},"events":3,"seed":1})");
  const Outcome r = run("dev-run --threads 2 --spec \"" + spec.path() + "\"");
  CHECK(r.code == 0);
  CHECK(r.out.rfind("EXPERIMENTAL dev-run", 0) == 0);
  CHECK(r.out.find("absorber_MeV") != std::string::npos);
  CHECK(r.out.find("fraction of incident energy") != std::string::npos);
  CHECK(r.out.find("threads 2") != std::string::npos);
  // Geant4 prints a banner and tables; none of it may reach stdout.
  CHECK(r.out.find("Geant4 version") == std::string::npos);
  CHECK(r.out.find("Cr_sctns") == std::string::npos);
}

TEST_CASE("dev-run exits 3 when a data set is missing", "[cli]")
{
  const std::string command = "env -u G4LEDATA \"" + std::string(VIKSHEP_SIM_EXE) +
                              "\" dev-run --spec \"" + example_path() + "\" 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[256];
  std::string out;
  while (fgets(buffer, sizeof buffer, pipe) != nullptr) {
    out += buffer;
  }
  const int status = pclose(pipe);
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 3);
  CHECK(out.empty());
}

TEST_CASE("dev-run with an invalid spec exits 2 with the error line", "[cli]")
{
  const TempSpec spec(R"({"spec_version":"1.0"})");
  const Outcome r = run("dev-run --spec \"" + spec.path() + "\"");
  CHECK(r.code == 2);
  CHECK(json::parse(r.out).at("code") == "SPEC_INVALID");
}

TEST_CASE("dev-run rejects --threads 0", "[cli]")
{
  const Outcome r = run("dev-run --threads 0 --spec \"" + example_path() + "\"");
  CHECK(r.code == 2);
  CHECK(r.out.empty());
}
