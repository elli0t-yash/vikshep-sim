#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tracks.hh"

// The optional 3-D export of `run --export-3d` (docs/EXPORT3D.md): geometry.glb and
// trajectories.json. For looking only; a subset of the simulation; not part of the OID.

constexpr int kExportEventsDefault = 10;
constexpr int kExportEventsMax = 100;
constexpr int kExportMaxTracksPerEvent = 500;
constexpr double kExportMinEnergyDefaultGeV = 0.001;
constexpr double kExportMinEnergyMaxGeV = 10000.0;
constexpr int kTubeSegments = 64;
constexpr const char* kExportFormatVersion = "1.0";
constexpr const char* kGeometryFileName = "geometry.glb";
constexpr const char* kTrajectoriesFileName = "trajectories.json";

struct ExportOptions {
  bool enabled = false;
  int events = kExportEventsDefault;                      // first N events by event_id
  double min_energy_gev = kExportMinEnergyDefaultGeV;     // secondaries below this are not kept
};

// An exported file, as listed in provenance.json and run_summary.json.
struct ExportedFile {
  std::string name;
  std::uint64_t bytes = 0;
  std::string oid;  // the OID rule applied to this file's bytes (not the OID of hits.arrow)
};

// ---- trajectories.json

// Applies the count limit of EXPORT3D.md, section 4.3, to the tracks that passed the energy test
// (every primary and every track at or above the minimum energy): at most kExportMaxTracksPerEvent,
// primaries first, then by descending energy, ties by ascending track_id; the result is in
// ascending track_id. tracks_total is the number of tracks Geant4 created in the event.
EventTracks select_tracks(std::vector<TrackRecord> eligible, std::uint32_t tracks_total);

// printf "%.<decimals>f" with "-0.000" written as "0.000". Throws ExportError for NaN and infinity.
std::string format_fixed(double value, int decimals);

// The first line of the file, up to and including the `[` of the events array, with its newline.
std::string trajectories_header(const std::string& spec_oid, const ExportOptions& options);

// One event object (section 3.2), without a trailing newline or comma.
std::string trajectories_event_json(std::uint64_t event_id, const EventTracks& event);

// Writes trajectories.json event by event, in the order add_event is called (event_id order).
// Every failure is an ExportError.
class TrajectoryWriter {
 public:
  TrajectoryWriter(const std::string& path, const std::string& spec_oid, const ExportOptions& options);
  void add_event(std::uint64_t event_id, const EventTracks& event);
  // Closes the file and returns its size and OID. Call once.
  ExportedFile finish();

 private:
  std::string path_;
  std::ofstream out_;
  std::uint64_t events_ = 0;
};

// ---- geometry.glb

// The bytes of geometry.glb for a resolved spec (resolve_spec(), either template). Deterministic.
std::string build_geometry_glb(const nlohmann::json& resolved_spec);

// Writes it to path. Every failure is an ExportError.
ExportedFile write_geometry_glb(const std::string& path, const nlohmann::json& resolved_spec);

// The spec_oid of a resolved spec (SIDECAR.md, section 5.1), as provenance.json has it.
std::string spec_oid_of(const nlohmann::json& resolved_spec);

// The entry of a file in provenance.json / run_summary.json: {"bytes", "file", "oid"}.
nlohmann::json exported_file_json(const ExportedFile& file);
