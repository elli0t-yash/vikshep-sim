// Runs of `vikshep-sim dev-run --event-digests`: determinism of the hits.
// Each run is a separate process (a Geant4 run manager is process-global state).

#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cli_helpers.hh"
#include "data_sets_guard.hh"
#include "datasets_check.hh"

using nlohmann::json;

namespace {

// Skips (visibly, never silently) when the Geant4 data sets are missing.

json example_spec()
{
  std::ifstream in(cli::example_path());
  return json::parse(in);
}

// A small, quick variant of the example: 1 GeV electrons.
json base_spec(int events, std::uint64_t seed)
{
  json s = example_spec();
  s["events"] = events;
  s["gun"]["energy_GeV"] = 1;
  s["seed"] = seed;
  return s;
}

// Runs dev-run and returns the digest lines, parsed. Fails the test if the run fails.
std::vector<json> digests(const json& spec, int threads, std::vector<std::string>* raw = nullptr)
{
  const cli::TempFile spec_file(spec.dump());
  const cli::TempFile out_file("", ".jsonl");
  const cli::Outcome r = cli::run("dev-run --threads " + std::to_string(threads) + " --spec \"" +
                                  spec_file.path() + "\" --event-digests \"" + out_file.path() +
                                  "\"");
  REQUIRE(r.code == 0);
  std::ifstream in(out_file.path());
  std::vector<json> lines;
  std::string line;
  while (std::getline(in, line)) {
    if (raw != nullptr) {
      raw->push_back(line);
    }
    lines.push_back(json::parse(line));
  }
  return lines;
}

}  // namespace

TEST_CASE("one digest line per event, in event order", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const auto lines = digests(base_spec(12, 42), 3);
  REQUIRE(lines.size() == 12);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    CHECK(lines[i].at("event_id") == i);
    CHECK(lines[i].at("hash").get<std::string>().size() == 16);
    CHECK(lines[i].at("hits").get<std::uint64_t>() > 0);
  }
}

TEST_CASE("1 thread and 4 threads give identical digests", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  std::vector<std::string> one, four;
  const auto a = digests(base_spec(20, 42), 1, &one);
  const auto b = digests(base_spec(20, 42), 4, &four);
  REQUIRE(a.size() == 20);
  REQUIRE(b.size() == 20);
  for (std::size_t i = 0; i < a.size(); ++i) {
    INFO("event " << i);
    CHECK(one[i] == four[i]);  // the whole line: hash, count and energy sums
  }
}

TEST_CASE("an event does not depend on the run it is in", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  std::vector<std::string> short_run, long_run;
  digests(base_spec(5, 42), 2, &short_run);
  digests(base_spec(20, 42), 3, &long_run);
  REQUIRE(short_run.size() == 5);
  REQUIRE(long_run.size() == 20);
  for (std::size_t i = 0; i < 5; ++i) {
    INFO("event " << i);
    CHECK(short_run[i] == long_run[i]);
  }
}

TEST_CASE("a different seed gives different events", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const auto a = digests(base_spec(5, 42), 2);
  const auto b = digests(base_spec(5, 43), 2);
  REQUIRE(a.size() == 5);
  REQUIRE(b.size() == 5);
  for (std::size_t i = 0; i < 5; ++i) {
    INFO("event " << i);
    CHECK(a[i].at("hash") != b[i].at("hash"));
  }
}

TEST_CASE("sensitive [absorber] gives hits only in the absorber", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  json spec = base_spec(6, 42);
  spec["sensitive"] = json::array({"absorber"});
  const auto lines = digests(spec, 2);
  REQUIRE(lines.size() == 6);
  for (const json& line : lines) {
    REQUIRE(line.at("hits_by_volume").size() == 1);
    CHECK(line.at("hits_by_volume").contains("absorber"));
    CHECK_FALSE(line.at("hits_by_volume").contains("active"));
    CHECK(line.at("hits").get<std::uint64_t>() == line.at("hits_by_volume").at("absorber").get<std::uint64_t>());
    CHECK(line.at("hits").get<std::uint64_t>() > 0);
  }
}

TEST_CASE("sensitive [active] (the default) gives hits only in the active slabs", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const auto lines = digests(base_spec(6, 42), 2);  // `sensitive` omitted
  REQUIRE(lines.size() == 6);
  for (const json& line : lines) {
    REQUIRE(line.at("hits_by_volume").size() == 1);
    CHECK(line.at("hits_by_volume").contains("active"));
    CHECK(line.at("hits").get<std::uint64_t>() > 0);
  }
}

TEST_CASE("both volumes sensitive: hits in both", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  json spec = base_spec(6, 42);
  spec["sensitive"] = json::array({"active", "absorber"});
  const auto lines = digests(spec, 2);
  REQUIRE(lines.size() == 6);
  for (const json& line : lines) {
    CHECK(line.at("hits_by_volume").at("absorber").get<std::uint64_t>() > 0);
    CHECK(line.at("hits_by_volume").at("active").get<std::uint64_t>() > 0);
  }
}

TEST_CASE("events without hits are handled and still get a line", "[digests][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  // The gun is aimed along +x from inside the vacuum of the world: the electron
  // leaves through the side without touching the stack.
  json spec = base_spec(6, 42);
  spec["gun"]["direction"] = json::array({1, 0, 0});
  std::vector<std::string> raw;
  const auto lines = digests(spec, 2, &raw);
  REQUIRE(lines.size() == 6);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    INFO("event " << i);
    CHECK(lines[i].at("event_id") == i);
    CHECK(lines[i].at("hits") == 0);
    CHECK(lines[i].at("edep_sum_MeV") == 0.0);
    CHECK(lines[i].at("hits_by_volume").at("active") == 0);
    CHECK(lines[i].at("hash") == "cbf29ce484222325");  // FNV offset basis: no rows
  }
}

TEST_CASE("an unwritable digest file is a failure, not a crash", "[digests]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec(base_spec(2, 1).dump());
  const cli::Outcome r = cli::run("dev-run --spec \"" + spec.path() +
                                  "\" --event-digests /nonexistent-directory/digests.jsonl");
  CHECK(r.code == 1);
  CHECK(r.out.empty());
}
