#pragma once

#include <cstdint>
#include <vector>

// One row of hits.arrow (docs/HITS.md, section 2), in the units of that
// document: mm, ns, MeV, MeV/c.
struct Hit {
  int volume = 0;  // index into kVolumeNames: 0 "absorber", 1 "active", 2 "tracker_layer"
  std::int32_t copy_no = 0;
  std::int32_t layer = -1;  // layer index, 0 = front (calorimeter) or innermost (tracker); -1 if none
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

// Names of the sensitive volumes of all templates, sorted bytewise (the order of the volume
// dictionary, docs/HITS.md section 5.3). A run uses the subset that its spec makes sensitive.
constexpr int kVolumeCount = 3;
constexpr const char* kVolumeNames[kVolumeCount] = {"absorber", "active", "tracker_layer"};
constexpr int kVolumeAbsorber = 0;
constexpr int kVolumeActive = 1;
constexpr int kVolumeTrackerLayer = 2;
