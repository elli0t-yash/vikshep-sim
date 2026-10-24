// Test-only program. Runs a small calorimeter spec in which a "gun" also draws a
// random number for every event, right after the event was seeded, and prints
// the numbers. tests/check_random_gun.py runs it with different thread counts and
// event counts and compares the output.
//
// Usage: random-gun-probe <threads> <events> <seed>
// Output, one line per event: <event id> <drawn value> <expected value>
// where "expected" is what a freshly seeded engine gives for that (seed, event id).
// Exit code 77: Geant4 data sets are missing (ctest "skipped").

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Randomize.hh"
#include "datasets_check.hh"
#include "dev_run.hh"
#include "event_seed.hh"
#include "spec_canonical.hh"

int main(int argc, char** argv)
{
  if (argc != 4) {
    std::fprintf(stderr, "usage: random-gun-probe <threads> <events> <seed>\n");
    return 2;
  }
  const int threads = std::atoi(argv[1]);
  const int events = std::atoi(argv[2]);
  const std::uint64_t seed = std::strtoull(argv[3], nullptr, 10);

  const auto missing = find_missing_data_sets(required_data_sets("FTFP_BERT"));
  if (!missing.empty()) {
    const char* require = std::getenv("VIKSHEP_REQUIRE_DATA");
    const bool must = require != nullptr && *require != '\0' && std::string(require) != "0";
    std::fprintf(stderr, "%s: Geant4 data sets are missing. %s\n",
                 must ? "FAILED (VIKSHEP_REQUIRE_DATA is set)" : "SKIPPED, not passed", describe_missing(missing).c_str());
    return must ? 1 : 77;
  }

  std::ifstream in(std::string(VIKSHEP_EXAMPLES_DIR) + "/calorimeter.json");
  nlohmann::json spec = nlohmann::json::parse(in);
  spec["geometry"]["layers"] = 2;
  spec["gun"]["energy_GeV"] = 0.2;
  spec["events"] = events;
  spec["seed"] = seed;

  std::vector<double> drawn(static_cast<std::size_t>(events), -1.0);
  run_dev_simulation(resolve_spec(spec), threads, nullptr, [&](std::uint64_t id) {
    // Each event writes only its own slot.
    drawn[id] = CLHEP::HepRandom::getTheEngine()->flat();
  });

  for (int e = 0; e < events; ++e) {
    seed_event_engine(seed, static_cast<std::uint64_t>(e));
    const double expected = CLHEP::HepRandom::getTheEngine()->flat();
    std::printf("%d %.17g %.17g\n", e, drawn[static_cast<std::size_t>(e)], expected);
  }
  return 0;
}
