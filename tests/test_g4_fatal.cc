// A fatal Geant4 exception during `run` becomes one SIMULATION_FAILED error line and exit code 1,
// with no output folder and no core dump. The failure is raised on purpose through the test-only
// environment variable VIKSHEP_TEST_G4_FATAL (src/dev_run.cc), on a worker thread or on the master.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>
#include <iterator>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cli_helpers.hh"
#include "data_sets_guard.hh"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

json spec()
{
  std::ifstream in(cli::example_path());
  json s = json::parse(in);
  s["events"] = 60;
  return s;
}

struct Outcome {
  std::string out;
  int exit_code = -1;
  bool exited = false;      // false if the process was killed by a signal (abort() would be SIGABRT)
};

Outcome run_with(const std::string& trigger, const std::string& spec_path, const std::string& out, const std::string& progress)
{
  const std::string command = "VIKSHEP_TEST_G4_FATAL=" + trigger + " \"" + VIKSHEP_SIM_EXE + "\" run --spec \"" +
                              spec_path + "\" --out \"" + out + "\" --threads 3 --progress " + progress + " 2>/dev/null";
  Outcome result;
  FILE* pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[4096];
  std::size_t n = 0;
  while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0) {
    result.out.append(buffer, n);
  }
  const int status = pclose(pipe);
  result.exited = WIFEXITED(status);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

std::vector<json> lines_of(const std::string& text)
{
  std::vector<json> lines;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(json::parse(line));  // a line that is not JSON fails the test
  }
  return lines;
}

}  // namespace

TEST_CASE("a fatal Geant4 exception on a worker thread is SIMULATION_FAILED, exit 1", "[g4fatal][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec_file(spec().dump());
  cli::TempDir dir;
  const Outcome r = run_with("event:7", spec_file.path(), dir.out(), "jsonl");
  CHECK(r.exited);            // not killed by SIGABRT
  CHECK(r.exit_code == 1);
  const auto lines = lines_of(r.out);  // every stdout line is JSON
  REQUIRE_FALSE(lines.empty());
  CHECK(lines.back().at("event") == "error");
  CHECK(lines.back().at("code") == "SIMULATION_FAILED");
  CHECK(lines.back().at("message").get<std::string>().find("TEST000") == std::string::npos);  // detail is on stderr
  int errors = 0;
  for (const json& l : lines) {
    CHECK(l.at("event") != "done");
    errors += l.at("event") == "error" ? 1 : 0;
  }
  CHECK(errors == 1);  // exactly one error line, even with three threads
  CHECK(std::distance(fs::directory_iterator(dir.path()), fs::directory_iterator()) == 0);  // no folder, no temporary folder
}

TEST_CASE("a fatal Geant4 exception on the master thread is SIMULATION_FAILED, exit 1", "[g4fatal][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec_file(spec().dump());
  cli::TempDir dir;
  const Outcome r = run_with("construct", spec_file.path(), dir.out(), "jsonl");
  CHECK(r.exited);
  CHECK(r.exit_code == 1);
  const auto lines = lines_of(r.out);
  REQUIRE_FALSE(lines.empty());
  CHECK(lines.back().at("code") == "SIMULATION_FAILED");
  CHECK(std::distance(fs::directory_iterator(dir.path()), fs::directory_iterator()) == 0);
}

TEST_CASE("with --progress none the failure leaves stdout empty", "[g4fatal][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec_file(spec().dump());
  cli::TempDir dir;
  const Outcome r = run_with("event:5", spec_file.path(), dir.out(), "none");
  CHECK(r.exited);
  CHECK(r.exit_code == 1);
  CHECK(r.out.empty());
  CHECK(std::distance(fs::directory_iterator(dir.path()), fs::directory_iterator()) == 0);
}
