#pragma once

#include <array>
#include <cstdint>
#include <vector>

// One particle track as the 3-D export keeps it (docs/EXPORT3D.md, section 3.3). Plain data, no
// Geant4: it travels from the worker thread that simulated the event to the writer through the
// reorder buffer, inside EventHits.
struct TrackRecord {
  std::int32_t track_id = 0;
  std::int32_t parent_id = 0;
  std::int32_t pdg = 0;
  std::int32_t charge = 0;                  // in units of the elementary charge
  double kinetic_energy_start_gev = 0.0;
  std::vector<std::array<double, 3>> points;  // mm, global frame: creation point, then every post-step point
};

// The tracks of one event that the export keeps, and how many Geant4 created in all
// (docs/EXPORT3D.md, sections 3.2 and 4).
struct EventTracks {
  std::uint32_t tracks_total = 0;
  std::vector<TrackRecord> tracks;  // ascending track_id

  std::uint32_t dropped() const { return tracks_total - static_cast<std::uint32_t>(tracks.size()); }
};
