// A real Geant4 run, in its own executable: a Geant4 run manager is process
// global state, so this file holds exactly one simulation.

#include <algorithm>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "datasets_check.hh"
#include "dev_run.hh"
#include "spec_canonical.hh"

using nlohmann::json;

TEST_CASE("a small calorimeter run is physically sane", "[geant4]")
{
  std::ifstream in(std::string(VIKSHEP_EXAMPLES_DIR) + "/calorimeter.json");
  REQUIRE(in.good());
  json spec = json::parse(in);
  spec["events"] = 20;
  spec["gun"]["energy_GeV"] = 1;
  spec["seed"] = 7;

  const json resolved = resolve_spec(spec);
  const auto missing = find_missing_data_sets(
      required_data_sets(resolved.at("physics").at("list").get<std::string>()));
  if (!missing.empty()) {
    SKIP("SKIPPED, not passed: Geant4 data sets are missing. " << describe_missing(missing));
  }

  const DevRunResult r = run_dev_simulation(resolved, 1);
  REQUIRE(r.events == 20);
  const int layers = r.geometry.layers;

  double fraction_sum = 0.0;
  double absorber_sum = 0.0;
  double active_sum = 0.0;
  for (int e = 0; e < r.events; ++e) {
    double total = 0.0;
    for (int layer = 0; layer < layers; ++layer) {
      const double a = r.edep(e, 0, layer);
      const double g = r.edep(e, 1, layer);
      CHECK(a >= 0.0);
      CHECK(g >= 0.0);
      absorber_sum += a;
      active_sum += g;
      total += a + g;
    }
    INFO("event " << e << " total " << total << " MeV");
    CHECK(total > 0.0);
    // Energy cannot be created. A tiny allowance for rounding in the sum.
    CHECK(total <= r.incident_energy_mev * (1.0 + 1e-9));
    fraction_sum += total / r.incident_energy_mev;
  }

  // Threshold justified in STATUS.md (measured 2026-10-24, several seeds): the
  // mean contained fraction of 1 GeV electrons is well above 0.9, so 0.8 is
  // loose while still failing if the stack were missing or mostly vacuum.
  CHECK(fraction_sum / r.events > 0.8);

  // Lead absorbs most of the energy; liquid argon is the thin sampling layer.
  CHECK(absorber_sum > active_sum);

  // Recorded run settings.
  CHECK(r.default_cut_mm == 0.7);
}
