#pragma once

#include <algorithm>
#include <string>
#include <vector>

// Dimensions of the tracker_barrel template (docs/SPEC.md, section 2.5). Plain numbers, no
// Geant4: shared by the validator, the canonical form and the detector construction.

constexpr double kTrackerWorldMarginMm = 100.0;  // vacuum around the barrel on every side
constexpr int kTrackerLayersMax = 100;
constexpr double kTrackerRadiusMaxMm = 10000.0;
constexpr double kTrackerHalfLengthMaxMm = 10000.0;
constexpr double kFieldMaxT = 10.0;  // largest accepted length of the field vector, tesla
constexpr const char* kTrackerDefaultMaterial = "G4_Si";
constexpr const char* kTrackerVolumeName = "tracker_layer";

struct TrackerLayer {
  double radius_mm = 0.0;       // middle of the wall
  double thickness_mm = 0.0;    // radial wall thickness
  double half_length_mm = 0.0;  // along z
  std::string material = kTrackerDefaultMaterial;

  double inner_radius_mm() const { return radius_mm - 0.5 * thickness_mm; }
  double outer_radius_mm() const { return radius_mm + 0.5 * thickness_mm; }
};

struct TrackerGeometry {
  std::vector<TrackerLayer> layers;  // index 0 is the innermost

  // The world is a box centred on the origin (docs/SPEC.md, 2.5). Full sizes, like the
  // calorimeter's.
  double max_outer_radius_mm() const
  {
    double r = 0.0;
    for (const TrackerLayer& l : layers) {
      r = std::max(r, l.outer_radius_mm());
    }
    return r;
  }
  double max_half_length_mm() const
  {
    double h = 0.0;
    for (const TrackerLayer& l : layers) {
      h = std::max(h, l.half_length_mm);
    }
    return h;
  }
  double world_transverse_mm() const { return 2.0 * (max_outer_radius_mm() + kTrackerWorldMarginMm); }
  double world_length_mm() const { return 2.0 * (max_half_length_mm() + kTrackerWorldMarginMm); }
};
