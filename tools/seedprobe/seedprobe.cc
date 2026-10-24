// Measures how Geant4 seeds events in multithreaded mode. Not part of the
// vikshep-sim build; see docs/notes-B4.md for how to build and run it.
//
// It runs N cheap events (an electron fired through vacuum, no interactions)
// and prints, for every event, the global event id the worker sees, the worker
// thread, and a hash of the thread's random engine state at two points: when
// the primary vertex is generated, and when the event action begins. No random
// number is drawn by the probe, so the state is what Geant4 set.
//
// Usage: seedprobe <events> <master seed> <threads> [manager: MT|Tasking|Serial]
// Output (stderr): lines "SEEDPROBE gen|begin event=<id> thread=<id> state=<hex>"
// and one "SEEDPROBE manager ..." line with the run manager's settings.

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <sstream>
#include <string>

#include "FTFP_BERT.hh"
#include "G4Box.hh"
#include "G4Event.hh"
#include "G4LogicalVolume.hh"
#include "G4MTRunManager.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4RunManagerFactory.hh"
#include "G4SystemOfUnits.hh"
#include "G4Threading.hh"
#include "G4UImanager.hh"
#include "G4UserEventAction.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "Randomize.hh"

namespace {

std::size_t engine_state_hash()
{
  std::ostringstream os;
  CLHEP::HepRandom::getTheEngine()->put(os);  // does not draw a number
  return std::hash<std::string>()(os.str());
}

void log(const char* where, const G4Event* event)
{
  std::fprintf(stderr, "SEEDPROBE %-5s event=%d thread=%d state=%zx\n", where, event->GetEventID(),
               G4Threading::G4GetThreadId(), engine_state_hash());
}

class Detector : public G4VUserDetectorConstruction {
 public:
  G4VPhysicalVolume* Construct() override
  {
    auto* vacuum = G4NistManager::Instance()->FindOrBuildMaterial("G4_Galactic");
    auto* solid = new G4Box("world", 1 * m, 1 * m, 1 * m);
    auto* lv = new G4LogicalVolume(solid, vacuum, "world");
    return new G4PVPlacement(nullptr, G4ThreeVector(), lv, "world", nullptr, false, 0, false);
  }
};

class Generator : public G4VUserPrimaryGeneratorAction {
 public:
  Generator() : gun_(1)
  {
    gun_.SetParticleDefinition(G4ParticleTable::GetParticleTable()->FindParticle("e-"));
    gun_.SetParticleEnergy(1 * MeV);
    gun_.SetParticleMomentumDirection(G4ThreeVector(1, 0, 0));
  }
  void GeneratePrimaries(G4Event* event) override
  {
    log("gen", event);
    gun_.GeneratePrimaryVertex(event);
  }

 private:
  G4ParticleGun gun_;
};

class EventAction : public G4UserEventAction {
 public:
  void BeginOfEventAction(const G4Event* event) override { log("begin", event); }
};

class Actions : public G4VUserActionInitialization {
 public:
  void Build() const override
  {
    SetUserAction(new Generator);
    SetUserAction(new EventAction);
  }
};

}  // namespace

int main(int argc, char** argv)
{
  if (argc < 4) {
    std::fprintf(stderr, "usage: seedprobe <events> <master seed> <threads> [MT|Tasking|Serial]\n");
    return 2;
  }
  const int events = std::atoi(argv[1]);
  const long seed = std::atol(argv[2]);
  const int threads = std::atoi(argv[3]);
  const std::string type = argc > 4 ? argv[4] : "MT";

  const G4RunManagerType kind = type == "Tasking" ? G4RunManagerType::TaskingOnly
                                : type == "Serial" ? G4RunManagerType::SerialOnly
                                                   : G4RunManagerType::MTOnly;
  std::unique_ptr<G4RunManager> manager(G4RunManagerFactory::CreateRunManager(kind, threads));
  if (kind != G4RunManagerType::SerialOnly) {
    manager->SetNumberOfThreads(threads);
  }
  G4UImanager::GetUIpointer()->ApplyCommand("/run/verbose 0");
  manager->SetUserInitialization(new Detector);
  manager->SetUserInitialization(new FTFP_BERT(0));
  manager->SetUserInitialization(new Actions);
  CLHEP::HepRandom::setTheSeed(seed);
  manager->Initialize();

  if (auto* mt = dynamic_cast<G4MTRunManager*>(manager.get())) {
    std::fprintf(stderr, "SEEDPROBE manager type=%s SeedOncePerCommunication=%d EventModuloDef=%d\n",
                 type.c_str(), G4MTRunManager::SeedOncePerCommunication(), mt->GetEventModulo());
  }
  else {
    std::fprintf(stderr, "SEEDPROBE manager type=%s\n", type.c_str());
  }
  manager->BeamOn(events);
  return 0;
}
