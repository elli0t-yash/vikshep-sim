// Exit codes and stdout shape of the vikshep-sim executable (POSIX only).

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "spec_canonical.hh"

using nlohmann::json;

namespace {

struct Outcome {
  std::string out;  // stdout
  int code = -1;    // exit code
};

// Runs vikshep-sim with the arguments. stderr is discarded.
Outcome run(const std::string& args)
{
  const std::string command = std::string("\"") + VIKSHEP_SIM_EXE + "\" " + args + " 2>/dev/null";
  Outcome result;
  FILE* pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[4096];
  std::size_t n = 0;
  while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0) {
    result.out.append(buffer, n);
  }
  const int status = pclose(pipe);
  result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

std::string example_path()
{
  return std::string(VIKSHEP_EXAMPLES_DIR) + "/calorimeter.json";
}

// A spec file in the temporary directory, removed when the object dies.
class TempSpec {
 public:
  explicit TempSpec(const std::string& text)
  {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("vikshep-test-spec-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) +
             ".json");
    std::ofstream(path_, std::ios::binary) << text;
  }
  ~TempSpec() { std::filesystem::remove(path_); }
  std::string path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

int line_count(const std::string& s)
{
  int n = 0;
  for (char c : s) {
    n += c == '\n' ? 1 : 0;
  }
  return n;
}

}  // namespace

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
