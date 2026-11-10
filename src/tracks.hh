#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Process names are stored as small ids (a name is repeated for thousands of points). The ids are
// handed out in the order names are first seen, which depends on the thread schedule, so an id is
// never written to a file and never used for ordering: only the name is.
constexpr std::uint16_t kNoProcess = 0xFFFF;
std::uint16_t export_process_id(const std::string& name);
const std::string& export_process_name(std::uint16_t id);  // kNoProcess gives "none"

// One interaction of a track (docs/EXPORT3D.md, section 7.4).
struct InteractionRecord {
  std::int32_t track_id = 0;
  std::int32_t point_index = 0;             // the point of the track that ends the step
  double t_ns = 0.0;
  std::array<double, 3> position_mm{};
  std::uint16_t process = kNoProcess;
  std::vector<std::int32_t> secondaries;    // track ids; filled when the secondaries start tracking
  double edep_gev = 0.0;
  double transferred_gev = 0.0;             // edep plus the creation energies of the secondaries (ranking only)
};

// One particle track as the 3-D export keeps it (docs/EXPORT3D.md, sections 3.3 and 7.3). Plain
// data, no Geant4: it travels from the worker thread that simulated the event to the writer
// through the reorder buffer, inside EventHits. The vectors t_ns, kinetic_energy_gev, edep_gev and
// process have one element per point; element 0 belongs to the creation point (edep 0, no process).
struct TrackRecord {
  std::int32_t track_id = 0;
  std::int32_t parent_id = 0;
  std::int32_t pdg = 0;
  std::int32_t charge = 0;                  // in units of the elementary charge
  double kinetic_energy_start_gev = 0.0;
  std::string particle;                     // Geant4 particle name
  std::uint16_t creator_process = kNoProcess;
  std::vector<std::array<double, 3>> points;  // mm, global frame: creation point, then every post-step point
  std::vector<double> t_ns;
  std::vector<double> kinetic_energy_gev;
  std::vector<double> edep_gev;
  std::vector<std::uint16_t> process;
  std::vector<InteractionRecord> interactions;  // moved to EventTracks::interactions when the event is selected
};

// The tracks of one event that the export keeps, and how many Geant4 created in all
// (docs/EXPORT3D.md, sections 3.2 and 4).
struct EventTracks {
  std::uint32_t tracks_total = 0;
  std::vector<TrackRecord> tracks;  // ascending track_id
  std::uint32_t interactions_total = 0;           // of all tracks of the event, kept or not
  std::vector<InteractionRecord> interactions;    // of the kept tracks, at most the cap, in output order

  std::uint32_t dropped() const { return tracks_total - static_cast<std::uint32_t>(tracks.size()); }
  std::uint32_t dropped_interactions() const
  {
    return interactions_total - static_cast<std::uint32_t>(interactions.size());
  }
};
