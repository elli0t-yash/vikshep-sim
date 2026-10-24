#include "spec_catalogue.hh"

#include "G4NistManager.hh"
#include "G4PhysListFactory.hh"

Catalogue load_geant4_catalogue()
{
  Catalogue c;

  // Only lists names. No material is built.
  for (const G4String& name : G4NistManager::Instance()->GetNistMaterialNames()) {
    c.materials.push_back(name);
  }

  G4PhysListFactory factory;
  for (const G4String& name : factory.AvailablePhysLists()) {
    c.physics_lists.push_back(name);
  }
  for (const G4String& name : factory.AvailablePhysListsEM()) {
    if (!name.empty()) {
      c.em_options.push_back(name);
    }
  }
  return c;
}
