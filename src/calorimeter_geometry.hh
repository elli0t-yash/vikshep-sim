#pragma once

// Dimensions of the layered_calorimeter template (docs/SPEC.md, section 2.2).
// Plain numbers, no Geant4: shared by the validator, the canonical form and the
// detector construction.

constexpr double kWorldMarginMm = 200.0;   // world is this much longer / wider than the stack
constexpr double kGunStandoffMm = 10.0;    // default gun start is this far in front of the stack

struct CalorimeterGeometry {
  int layers = 0;
  double absorber_thickness_mm = 0.0;
  double active_thickness_mm = 0.0;
  double transverse_mm = 0.0;

  double layer_thickness_mm() const { return absorber_thickness_mm + active_thickness_mm; }
  double stack_length_mm() const { return layers * layer_thickness_mm(); }
  double world_transverse_mm() const { return transverse_mm + kWorldMarginMm; }
  double world_length_mm() const { return stack_length_mm() + kWorldMarginMm; }
  // Start of layer i (front face of its absorber) on the z axis.
  double layer_front_z_mm(int i) const { return -0.5 * stack_length_mm() + i * layer_thickness_mm(); }
  // Default gun start on the z axis: in front of the stack.
  double default_gun_z_mm() const { return -(0.5 * stack_length_mm() + kGunStandoffMm); }
};
