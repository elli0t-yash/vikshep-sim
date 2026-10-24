#pragma once

#include <cstdint>
#include <vector>

// One row of hits.arrow (docs/HITS.md, section 2), in the units of that
// document: mm, ns, MeV, MeV/c.
struct Hit {
  int volume = 0;  // slab type: 0 = "absorber", 1 = "active" (names: kSlabNames)
  std::int32_t copy_no = 0;
  std::int32_t layer = -1;  // -1 when the template has no layers
  double x_mm = 0.0;        // step midpoint
  double y_mm = 0.0;
  double z_mm = 0.0;
  double t_ns = 0.0;        // global time at the start of the step
  double edep_mev = 0.0;
  double px_mev = 0.0;      // momentum at the start of the step, MeV/c
  double py_mev = 0.0;
  double pz_mev = 0.0;
  std::int32_t track_id = 0;
  std::int32_t parent_id = 0;
  std::int32_t pdg = 0;
  std::int32_t step = 0;  // 1-based step number within the track
};

// All hits of one event, in step order.
struct EventHits {
  std::uint64_t event_id = 0;
  std::vector<Hit> hits;
};

// Slab type names, sorted bytewise (the order of the volume dictionary).
constexpr const char* kSlabNames[2] = {"absorber", "active"};
