#pragma once

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "calorimeter_geometry.hh"
#include "hits.hh"

// Runs the spec's geometry in Geant4 (layered_calorimeter, or tracker_barrel since spec 1.1) and
// keeps the energy deposited per event, slab type and layer. Used by `run` (through
// run_to_folder) and by the experimental `vikshep-sim dev-run`, which is outside the sidecar
// contract.

struct DevRunResult {
  // The calorimeter's numbers; all zero for a tracker_barrel spec.
  CalorimeterGeometry geometry;
  // Shape of the energy table: 2 slab types and `geometry.layers` layers for the calorimeter,
  // 1 "slab" (the tracker layer volume) and one entry per layer for the tracker.
  int slabs = 0;
  int layer_count = 0;
  int events = 0;
  int threads = 0;
  double incident_energy_mev = 0.0;
  std::string particle;
  std::string physics_list;
  std::string run_manager_type;  // see dev_run.cc
  double default_cut_mm = 0.0;   // default production cut in effect
  // Energy deposited, MeV: index (event * slabs + slab) * layer_count + layer. For the
  // calorimeter slab 0 = absorber and 1 = active.
  std::vector<double> edep_mev;

  double edep(int event, int slab, int layer) const
  {
    return edep_mev[(static_cast<std::size_t>(event) * slabs + slab) * layer_count + layer];
  }
};

// Receives the hits of each event, strictly in event_id order, each event once.
// Called from a worker thread, never from two threads at once. Events with no
// hits are delivered too, with an empty hits vector. May throw; the run then
// finishes quickly and run_dev_simulation() rethrows the first exception.
using EventConsumer = std::function<void(EventHits&&)>;

// Test hook: called with the event id on the worker thread, right after the
// event's random engine was seeded and before the gun fires. A test can draw a
// random number there to check that it is independent of the thread count.
using AfterSeeding = std::function<void(std::uint64_t event_id)>;

// Polled after every event on the worker threads. When it returns true the run is aborted
// softly: every thread finishes the event it is processing and starts no other. Events that
// were not simulated are not delivered, and run_dev_simulation() returns normally.
using CancelCheck = std::function<bool()>;

// resolved_spec: output of resolve_spec(). Nothing is written to stdout;
// Geant4's own output goes to stderr. Hits are recorded in the volumes listed in
// the spec's `sensitive` field and passed to consumer (if any). The energy table
// in the result covers both slab types regardless of `sensitive`.
DevRunResult run_dev_simulation(const nlohmann::json& resolved_spec, int threads,
                                const EventConsumer& consumer = nullptr,
                                const AfterSeeding& after_seeding = nullptr,
                                const CancelCheck& cancel = nullptr);
