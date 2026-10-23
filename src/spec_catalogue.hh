#pragma once

#include "spec_validate.hh"

// Reads material and physics list names from the installed Geant4. This does
// not create a run manager and needs no Geant4 data sets.
Catalogue load_geant4_catalogue();
