#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "calorimeter_geometry.hh"

// EXPERIMENTAL. Runs the layered calorimeter in Geant4 and keeps the energy
// deposited per event, slab type and layer. Used by `vikshep-sim dev-run`,
// which is outside the sidecar contract and will be replaced by `run`.

struct DevRunResult {
  CalorimeterGeometry geometry;
  int events = 0;
  int threads = 0;
  double incident_energy_mev = 0.0;
  std::string particle;
  std::string physics_list;
  std::string run_manager_type;  // see dev_run.cc
  double default_cut_mm = 0.0;   // default production cut in effect
  // Energy deposited, MeV: index (event * 2 + slab) * layers + layer, with
  // slab 0 = absorber and 1 = active.
  std::vector<double> edep_mev;

  double edep(int event, int slab, int layer) const
  {
    return edep_mev[(static_cast<std::size_t>(event) * 2 + slab) * geometry.layers + layer];
  }
};

// resolved_spec: output of resolve_spec(). Nothing is written to stdout;
// Geant4's own output goes to stderr.
DevRunResult run_dev_simulation(const nlohmann::json& resolved_spec, int threads);
