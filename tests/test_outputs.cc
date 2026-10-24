// Output folders of `vikshep-sim dev-run --out`: determinism, thread independence,
// pyarrow readability, atomicity. Each run is a separate process (a Geant4 run manager
// is process-global state).

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cli_helpers.hh"
#include "data_sets_guard.hh"
#include "datasets_check.hh"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {


std::string read_file(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

json read_json(const fs::path& path)
{
  return json::parse(read_file(path));
}

// A small spec: 3 layers, 0.5 GeV electrons, 10 events.
json small_spec(std::uint64_t seed, const std::vector<std::string>& sensitive = {})
{
  json s = {{"spec_version", "1.0"},
            {"template", "layered_calorimeter"},
            {"geometry",
             {{"layers", 3},
              {"absorber", {{"material", "G4_Pb"}, {"thickness_mm", 10}}},
              {"active", {{"material", "G4_lAr"}, {"thickness_mm", 5}}},
              {"transverse_mm", 100}}},
            {"gun", {{"particle", "e-"}, {"energy_GeV", 0.5}}},
            {"events", 10},
            {"seed", seed}};
  if (!sensitive.empty()) {
    s["sensitive"] = sensitive;
  }
  return s;
}

// Runs dev-run --out and returns the outcome.
cli::Outcome run_out(const json& spec, int threads, const std::string& out, const std::string& more = "")
{
  const cli::TempFile spec_file(spec.dump());
  return cli::run("dev-run --threads " + std::to_string(threads) + " --spec \"" + spec_file.path() +
                  "\" --out \"" + out + "\" " + more);
}

// run_summary.json without the two entries that differ from run to run.
json stable_summary(const fs::path& folder)
{
  json s = read_json(folder / "run_summary.json");
  s.erase("wall_time_s");
  s.erase("threads");
  return s;
}

// The pyarrow / hashlib checker; returns its exit code and output.
cli::Outcome check_folder(const std::string& folder, const std::string& sensitive,
                          const std::string& digests = "")
{
  std::string command = std::string("\"") + VIKSHEP_PYTHON + "\" \"" + VIKSHEP_TESTS_DIR +
                        "/check_output_folder.py\" \"" + folder + "\" --sensitive " + sensitive;
  if (!digests.empty()) {
    command += " --digests \"" + digests + "\"";
  }
  command += " 2>&1";
  cli::Outcome result;
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

std::vector<std::string> entries(const std::string& dir)
{
  std::vector<std::string> names;
  for (const auto& e : fs::directory_iterator(dir)) {
    names.push_back(e.path().filename().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace

TEST_CASE("a: the same spec and seed twice give identical outputs", "[outputs][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  REQUIRE(run_out(small_spec(42), 2, dir.out("a")).code == 0);
  REQUIRE(run_out(small_spec(42), 2, dir.out("b")).code == 0);
  const fs::path a = dir.out("a"), b = dir.out("b");
  CHECK(read_file(a / "hits.arrow") == read_file(b / "hits.arrow"));
  CHECK(read_file(a / "provenance.json") == read_file(b / "provenance.json"));
  // run_summary.json holds the wall time and the thread count, which differ from run to
  // run (docs/SIDECAR.md, 5.2). Everything else must be identical.
  CHECK(stable_summary(a) == stable_summary(b));
  CHECK(read_json(a / "run_summary.json").at("oid") == read_json(b / "run_summary.json").at("oid"));
}

TEST_CASE("b: 1 thread and 4 threads give identical outputs", "[outputs][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  const json spec = small_spec(42, {"absorber", "active"});
  REQUIRE(run_out(spec, 1, dir.out("one")).code == 0);
  REQUIRE(run_out(spec, 4, dir.out("four")).code == 0);
  const fs::path one = dir.out("one"), four = dir.out("four");
  CHECK(read_file(one / "hits.arrow") == read_file(four / "hits.arrow"));
  CHECK(read_file(one / "provenance.json") == read_file(four / "provenance.json"));  // no thread count in it
  CHECK(stable_summary(one) == stable_summary(four));
  CHECK(read_json(one / "run_summary.json").at("threads") == 1);
  CHECK(read_json(four / "run_summary.json").at("threads") == 4);
}

TEST_CASE("c: a different seed gives a different OID", "[outputs][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  REQUIRE(run_out(small_spec(42), 2, dir.out("a")).code == 0);
  REQUIRE(run_out(small_spec(43), 2, dir.out("b")).code == 0);
  CHECK(read_json(fs::path(dir.out("a")) / "run_summary.json").at("oid") !=
        read_json(fs::path(dir.out("b")) / "run_summary.json").at("oid"));
  CHECK(read_json(fs::path(dir.out("a")) / "provenance.json").at("seed") == 42);
  CHECK(read_json(fs::path(dir.out("b")) / "provenance.json").at("seed") == 43);
}

TEST_CASE("d and e: pyarrow reads the folder; energies equal the digests of the same run",
          "[outputs][geant4][pyarrow]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  const cli::TempFile digests("", ".jsonl");
  const cli::Outcome r = run_out(small_spec(7, {"absorber", "active"}), 3, dir.out(),
                                 "--event-digests \"" + digests.path() + "\"");
  REQUIRE(r.code == 0);
  CHECK(r.out.find("oid") != std::string::npos);
  const cli::Outcome check = check_folder(dir.out(), "absorber,active", digests.path());
  INFO(check.out);
  CHECK(check.code == 0);
}

TEST_CASE("d: pyarrow reads the folder with the default sensitive volume", "[outputs][geant4][pyarrow]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  REQUIRE(run_out(small_spec(8), 2, dir.out()).code == 0);
  const cli::Outcome check = check_folder(dir.out(), "active");
  INFO(check.out);
  CHECK(check.code == 0);
}

TEST_CASE("d: a run with several full record batches", "[outputs][geant4][pyarrow]")
{
  SKIP_WITHOUT_DATA_SETS();
  // 5 GeV, both volumes: about 12000 hits per event, so 20 events fill several batches.
  json spec = small_spec(9, {"absorber", "active"});
  spec["geometry"]["layers"] = 10;
  spec["gun"]["energy_GeV"] = 5;
  spec["events"] = 20;
  cli::TempDir dir;
  REQUIRE(run_out(spec, 4, dir.out()).code == 0);
  const json summary = read_json(fs::path(dir.out()) / "run_summary.json");
  CHECK(summary.at("hits_total").get<std::uint64_t>() > 2 * 65536);
  const cli::Outcome check = check_folder(dir.out(), "absorber,active");
  INFO(check.out);
  CHECK(check.code == 0);
}

TEST_CASE("f: a run without any hit gives a valid file and the right counts", "[outputs][geant4][pyarrow]")
{
  SKIP_WITHOUT_DATA_SETS();
  json spec = small_spec(1);
  spec["gun"]["direction"] = json::array({1, 0, 0});  // out of the world, never touching the stack
  cli::TempDir dir;
  REQUIRE(run_out(spec, 2, dir.out()).code == 0);
  const json summary = read_json(fs::path(dir.out()) / "run_summary.json");
  CHECK(summary.at("events_requested") == 10);
  CHECK(summary.at("events_completed") == 10);
  CHECK(summary.at("events_with_hits") == 0);
  CHECK(summary.at("events_without_hits") == 10);
  CHECK(summary.at("hits_total") == 0);
  // The file holds the schema and the dictionary and nothing else: it is the same
  // for every zero-hit run with sensitive ["active"].
  CHECK(read_file(fs::path(dir.out()) / "hits.arrow") ==
        read_file(fs::path(VIKSHEP_GOLDEN_DIR) / "hits_empty.arrow"));
  const cli::Outcome check = check_folder(dir.out(), "active");
  INFO(check.out);
  CHECK(check.code == 0);
}

TEST_CASE("h: an existing output folder is refused and left untouched", "[outputs]")
{
  // This needs no simulation: the check comes first. (Data sets are not needed.)
  cli::TempDir dir;
  const fs::path existing = dir.out("existing");
  fs::create_directories(existing);
  std::ofstream(existing / "marker.txt") << "keep me";
  const cli::Outcome r = run_out(small_spec(1), 1, existing.string());
  CHECK(r.code == 2);
  const json line = json::parse(r.out);
  CHECK(line.at("code") == "OUTPUT_EXISTS");
  CHECK(line.at("event") == "error");
  CHECK(entries(existing.string()) == std::vector<std::string>{"marker.txt"});
  CHECK(read_file(existing / "marker.txt") == "keep me");
  CHECK(entries(dir.path()) == std::vector<std::string>{"existing"});  // no temporary folder either

  // An existing file, and a missing parent, are refused too.
  std::ofstream(dir.out("file")) << "x";
  CHECK(run_out(small_spec(1), 1, dir.out("file")).code == 2);
  const cli::Outcome missing_parent = run_out(small_spec(1), 1, dir.out("no-such-parent/run"));
  CHECK(missing_parent.code == 2);
  CHECK(json::parse(missing_parent.out).at("code") == "OUTPUT_EXISTS");
}

TEST_CASE("i: a failure in the middle of a run leaves no folder behind", "[outputs][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  // Real I/O failure, injected from outside: the file size limit (ulimit -f) makes the first
  // full record batch fail to write, in the middle of the run. SIGXFSZ is ignored so that
  // the write returns an error instead of killing the process.
  json spec = small_spec(3, {"absorber", "active"});
  spec["geometry"]["layers"] = 10;
  spec["gun"]["energy_GeV"] = 5;
  spec["events"] = 20;
  const cli::TempFile spec_file(spec.dump());
  cli::TempDir dir;
  const cli::TempFile script(std::string("ulimit -f 400\ntrap '' XFSZ\nexec \"") + VIKSHEP_SIM_EXE +
                                 "\" dev-run --threads 3 --spec \"" + spec_file.path() + "\" --out \"" +
                                 dir.out() + "\"\n",
                             ".sh");
  const cli::Outcome r = [&] {
    cli::Outcome result;
    FILE* pipe = popen(("/bin/sh \"" + script.path() + "\" 2>/dev/null").c_str(), "r");
    REQUIRE(pipe != nullptr);
    char buffer[1024];
    std::size_t n = 0;
    while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0) {
      result.out.append(buffer, n);
    }
    const int status = pclose(pipe);
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
  }();
  CHECK(r.code == 1);  // a clean failure, not a crash
  CHECK_FALSE(fs::exists(dir.out()));
  CHECK(entries(dir.path()).empty());  // no ".run.partial-<pid>" either
}

TEST_CASE("a successful run leaves no temporary folder", "[outputs][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  REQUIRE(run_out(small_spec(5), 2, dir.out()).code == 0);
  CHECK(entries(dir.path()) == std::vector<std::string>{"run"});
}

TEST_CASE("dev-run without --out still prints the table and writes nothing", "[outputs][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec_file(small_spec(1).dump());
  const cli::Outcome r = cli::run("dev-run --spec \"" + spec_file.path() + "\"");
  CHECK(r.code == 0);
  CHECK(r.out.find("absorber_MeV") != std::string::npos);
  CHECK(r.out.find("Output folder written") == std::string::npos);
}
