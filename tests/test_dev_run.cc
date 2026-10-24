// A real Geant4 run, in its own executable: a Geant4 run manager is process
// global state, so this file holds exactly one simulation.

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "data_sets_guard.hh"
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
  spec["sensitive"] = json::array({"absorber", "active"});

  const json resolved = resolve_spec(spec);
  SKIP_WITHOUT_DATA_SETS();

  // Collect the hits the run delivers, to compare them with the energy table.
  std::vector<EventHits> delivered;
  const DevRunResult r = run_dev_simulation(
      resolved, 1, [&delivered](EventHits&& event) { delivered.push_back(std::move(event)); });
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

  // The hits: every event delivered once, in order, rows well formed.
  REQUIRE(delivered.size() == 20);
  std::array<double, 2> hit_sum = {0.0, 0.0};
  std::array<std::size_t, 2> hit_count = {0, 0};
  for (std::size_t e = 0; e < delivered.size(); ++e) {
    CHECK(delivered[e].event_id == e);
    // Within one track the steps come in increasing step order, times do not go back.
    std::map<int, std::pair<int, double>> last;  // track -> (step, time)
    for (const Hit& h : delivered[e].hits) {
      REQUIRE((h.volume == 0 || h.volume == 1));
      CHECK(h.edep_mev > 0.0);
      CHECK(h.layer == h.copy_no);
      CHECK(h.copy_no >= 0);
      CHECK(h.copy_no < layers);
      CHECK(h.track_id >= 1);
      CHECK(h.parent_id >= 0);
      CHECK(h.step >= 1);
      CHECK(h.t_ns >= 0.0);
      CHECK(std::abs(h.x_mm) < 0.5 * r.geometry.world_transverse_mm());
      CHECK(std::abs(h.y_mm) < 0.5 * r.geometry.world_transverse_mm());
      CHECK(std::abs(h.z_mm) < 0.5 * r.geometry.world_length_mm());
      // The hit lies inside its layer's slab (midpoint of a step in that slab).
      const double front = r.geometry.layer_front_z_mm(h.copy_no);
      const double lo = h.volume == 0 ? front : front + r.geometry.absorber_thickness_mm;
      const double hi = lo + (h.volume == 0 ? r.geometry.absorber_thickness_mm
                                            : r.geometry.active_thickness_mm);
      CHECK(h.z_mm >= lo - 1e-6);
      CHECK(h.z_mm <= hi + 1e-6);
      auto it = last.find(h.track_id);
      if (it != last.end()) {
        CHECK(h.step > it->second.first);
        CHECK(h.t_ns >= it->second.second);
      }
      last[h.track_id] = {h.step, h.t_ns};
      hit_sum[h.volume] += h.edep_mev;
      hit_count[h.volume] += 1;
    }
  }
  CHECK(hit_count[0] > 0);
  CHECK(hit_count[1] > 0);

  // Energy consistency: the summed hit energy per volume equals the totals of the
  // energy table of the same run, up to floating-point summation order.
  for (int slab = 0; slab < 2; ++slab) {
    double table = 0.0;
    for (int e = 0; e < r.events; ++e) {
      for (int layer = 0; layer < layers; ++layer) {
        table += r.edep(e, slab, layer);
      }
    }
    INFO("slab " << slab << " hits " << hit_sum[slab] << " table " << table);
    CHECK(std::abs(hit_sum[slab] - table) <= 1e-9 * table);
  }
}
