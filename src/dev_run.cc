#include "dev_run.hh"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

#include "G4Box.hh"
#include "G4EmParameters.hh"
#include "G4Event.hh"
#include "G4HadronicParameters.hh"
#include "G4LogicalVolume.hh"
#include "G4NistManager.hh"
#include "G4PVPlacement.hh"
#include "G4ParticleGun.hh"
#include "G4ParticleTable.hh"
#include "G4PhysListFactory.hh"
#include "G4RunManagerFactory.hh"
#include "G4SDManager.hh"
#include "G4Step.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4UImanager.hh"
#include "G4UserEventAction.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VSensitiveDetector.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "Randomize.hh"
#include "event_seed.hh"
#include "stderr_session.hh"

using nlohmann::json;

namespace {

// Run manager used: the classic multithreaded manager (G4MTRunManager), chosen
// explicitly and for any thread count, including 1. It is never the serial
// manager, because serial and multithreaded runs seed their events differently
// and the thread count must not change what is simulated. Geant4's "Default"
// type (tasking, or the G4RUN_MANAGER_TYPE environment variable) is not used.
constexpr G4RunManagerType kRunManagerType = G4RunManagerType::MTOnly;
constexpr const char* kRunManagerTypeName = "MT (G4MTRunManager)";

// Shared by all threads. Each event writes only its own slice.
struct SharedResults {
  CalorimeterGeometry geometry;
  std::uint64_t spec_seed = 0;
  std::vector<double>* edep_mev = nullptr;
};

// Energy deposited in the event being processed by this thread.
std::vector<double>& thread_event_buffer()
{
  static thread_local std::vector<double> buffer;
  return buffer;
}

class DetectorConstruction : public G4VUserDetectorConstruction {
 public:
  DetectorConstruction(const json& spec, const CalorimeterGeometry& geometry)
      : geometry_(geometry),
        absorber_material_(spec.at("geometry").at("absorber").at("material").get<std::string>()),
        active_material_(spec.at("geometry").at("active").at("material").get<std::string>())
  {
  }

  G4VPhysicalVolume* Construct() override
  {
    auto* nist = G4NistManager::Instance();
    G4Material* vacuum = nist->FindOrBuildMaterial("G4_Galactic");
    G4Material* absorber = nist->FindOrBuildMaterial(absorber_material_);
    G4Material* active = nist->FindOrBuildMaterial(active_material_);
    if (vacuum == nullptr || absorber == nullptr || active == nullptr) {
      throw std::runtime_error("a material of the spec is unknown to Geant4");
    }

    const double half_xy = 0.5 * geometry_.transverse_mm * mm;

    auto* world_solid = new G4Box("world", 0.5 * geometry_.world_transverse_mm() * mm,
                                  0.5 * geometry_.world_transverse_mm() * mm,
                                  0.5 * geometry_.world_length_mm() * mm);
    auto* world_lv = new G4LogicalVolume(world_solid, vacuum, "world");
    auto* world_pv = new G4PVPlacement(nullptr, G4ThreeVector(), world_lv, "world", nullptr, false,
                                       0, false);

    // One logical volume per slab type; one placement per slab, with the layer
    // index as copy number.
    absorber_lv_ = new G4LogicalVolume(
        new G4Box("absorber", half_xy, half_xy, 0.5 * geometry_.absorber_thickness_mm * mm),
        absorber, "absorber");
    active_lv_ = new G4LogicalVolume(
        new G4Box("active", half_xy, half_xy, 0.5 * geometry_.active_thickness_mm * mm), active,
        "active");

    for (int i = 0; i < geometry_.layers; ++i) {
      const double front = geometry_.layer_front_z_mm(i);
      const double absorber_z = front + 0.5 * geometry_.absorber_thickness_mm;
      const double active_z =
          front + geometry_.absorber_thickness_mm + 0.5 * geometry_.active_thickness_mm;
      new G4PVPlacement(nullptr, G4ThreeVector(0, 0, absorber_z * mm), absorber_lv_, "absorber",
                        world_lv, false, i, false);
      new G4PVPlacement(nullptr, G4ThreeVector(0, 0, active_z * mm), active_lv_, "active",
                        world_lv, false, i, false);
    }
    return world_pv;
  }

  void ConstructSDandField() override;  // below

 private:
  CalorimeterGeometry geometry_;
  std::string absorber_material_;
  std::string active_material_;
  G4LogicalVolume* absorber_lv_ = nullptr;
  G4LogicalVolume* active_lv_ = nullptr;
};

// Adds the energy deposited in a step to this thread's event buffer. Both slab
// types are scored here regardless of the spec's `sensitive` field, because
// dev-run reports both.
class SlabDetector : public G4VSensitiveDetector {
 public:
  SlabDetector(const std::string& name, int slab, int layers)
      : G4VSensitiveDetector(name), slab_(slab), layers_(layers)
  {
  }

  G4bool ProcessHits(G4Step* step, G4TouchableHistory*) override
  {
    const double edep = step->GetTotalEnergyDeposit();
    if (edep <= 0.0) {
      return false;
    }
    const int layer = step->GetPreStepPoint()->GetTouchable()->GetCopyNumber();
    std::vector<double>& buffer = thread_event_buffer();
    buffer[static_cast<std::size_t>(slab_) * layers_ + layer] += edep / MeV;
    return true;
  }

 private:
  int slab_;
  int layers_;
};

void DetectorConstruction::ConstructSDandField()
{
  auto* manager = G4SDManager::GetSDMpointer();
  auto* absorber_sd = new SlabDetector("absorber_sd", 0, geometry_.layers);
  auto* active_sd = new SlabDetector("active_sd", 1, geometry_.layers);
  manager->AddNewDetector(absorber_sd);
  manager->AddNewDetector(active_sd);
  SetSensitiveDetector("absorber", absorber_sd);
  SetSensitiveDetector("active", active_sd);
}

struct GunConfig {
  std::string particle;
  double energy_mev = 0.0;
  G4ThreeVector position_mm;
  G4ThreeVector direction;
};

class PrimaryGenerator : public G4VUserPrimaryGeneratorAction {
 public:
  explicit PrimaryGenerator(const GunConfig& config) : gun_(1)
  {
    G4ParticleDefinition* particle = G4ParticleTable::GetParticleTable()->FindParticle(config.particle);
    if (particle == nullptr) {
      throw std::runtime_error("Geant4 does not know the particle " + config.particle);
    }
    gun_.SetParticleDefinition(particle);
    gun_.SetParticleEnergy(config.energy_mev * MeV);  // kinetic energy
    gun_.SetParticlePosition(config.position_mm * mm);
    gun_.SetParticleMomentumDirection(config.direction);
  }

  void GeneratePrimaries(G4Event* event) override { gun_.GeneratePrimaryVertex(event); }

 private:
  G4ParticleGun gun_;
};

class EventAction : public G4UserEventAction {
 public:
  explicit EventAction(SharedResults* shared) : shared_(shared) {}

  void BeginOfEventAction(const G4Event* event) override
  {
    // The random stream of an event depends only on (spec seed, global event
    // id). Geant4 has already seeded this thread's engine from its own queue;
    // overwrite that, so the requirement holds by construction and does not
    // rest on how G4MTRunManager happens to hand out seeds. Nothing has drawn a
    // random number yet: the gun is deterministic. If a gun ever draws random
    // numbers, its GeneratePrimaries() must run after this seeding.
    seed_event_engine(shared_->spec_seed, static_cast<std::uint64_t>(event->GetEventID()));
    thread_event_buffer().assign(2 * static_cast<std::size_t>(shared_->geometry.layers), 0.0);
  }

  void EndOfEventAction(const G4Event* event) override
  {
    const std::size_t size = 2 * static_cast<std::size_t>(shared_->geometry.layers);
    const std::vector<double>& buffer = thread_event_buffer();
    std::copy(buffer.begin(), buffer.end(),
              shared_->edep_mev->begin() + static_cast<std::ptrdiff_t>(event->GetEventID() * size));
  }

 private:
  SharedResults* shared_;
};

class Actions : public G4VUserActionInitialization {
 public:
  Actions(const GunConfig& gun, SharedResults* shared) : gun_(gun), shared_(shared) {}

  void Build() const override
  {
    SetUserAction(new PrimaryGenerator(gun_));
    SetUserAction(new EventAction(shared_));
  }

 private:
  GunConfig gun_;
  SharedResults* shared_;
};

}  // namespace

DevRunResult run_dev_simulation(const json& spec, int threads)
{
  if (threads < 1) {
    throw std::invalid_argument("threads must be at least 1");
  }

  CalorimeterGeometry geometry;
  {
    const json& g = spec.at("geometry");
    geometry.layers = g.at("layers").get<int>();
    geometry.absorber_thickness_mm = g.at("absorber").at("thickness_mm").get<double>();
    geometry.active_thickness_mm = g.at("active").at("thickness_mm").get<double>();
    geometry.transverse_mm = g.at("transverse_mm").get<double>();
  }
  const int events = spec.at("events").get<int>();

  GunConfig gun;
  {
    const json& j = spec.at("gun");
    gun.particle = j.at("particle").get<std::string>();
    gun.energy_mev = j.at("energy_GeV").get<double>() * 1000.0;
    gun.position_mm = G4ThreeVector(j.at("position_mm")[0].get<double>(),
                                    j.at("position_mm")[1].get<double>(),
                                    j.at("position_mm")[2].get<double>());
    gun.direction = G4ThreeVector(j.at("direction")[0].get<double>(),
                                  j.at("direction")[1].get<double>(),
                                  j.at("direction")[2].get<double>());
  }

  DevRunResult result;
  result.geometry = geometry;
  result.events = events;
  result.threads = threads;
  result.incident_energy_mev = gun.energy_mev;
  result.particle = gun.particle;
  result.physics_list = spec.at("physics").at("list").get<std::string>();
  result.run_manager_type = kRunManagerTypeName;
  result.edep_mev.assign(static_cast<std::size_t>(events) * 2 * geometry.layers, 0.0);

  // Everything Geant4 prints goes to stderr. Installed before the run manager
  // exists, because creating it prints the banner.
  StdoutToStderr stdout_guard;
  StderrSession session;
  G4UImanager::GetUIpointer()->SetCoutDestination(&session);

  std::unique_ptr<G4RunManager> run_manager(
      G4RunManagerFactory::CreateRunManager(kRunManagerType, threads));
  run_manager->SetNumberOfThreads(threads);

  G4UImanager::GetUIpointer()->ApplyCommand("/run/verbose 0");
  G4EmParameters::Instance()->SetVerbose(0);
  G4HadronicParameters::Instance()->SetVerboseLevel(0);

  G4PhysListFactory factory;
  G4VModularPhysicsList* physics = factory.GetReferencePhysList(result.physics_list);
  if (physics == nullptr) {
    throw std::runtime_error("Geant4 does not know the physics list " + result.physics_list);
  }
  result.default_cut_mm = physics->GetDefaultCutValue() / mm;

  SharedResults shared;
  shared.geometry = geometry;
  shared.spec_seed = spec.at("seed").get<std::uint64_t>();
  shared.edep_mev = &result.edep_mev;

  run_manager->SetUserInitialization(new DetectorConstruction(spec, geometry));
  run_manager->SetUserInitialization(physics);
  run_manager->SetUserInitialization(new Actions(gun, &shared));

  // Geant4's master engine still seeds Geant4's own per-event seeds, which
  // EventAction replaces. Seed it from the spec anyway so nothing depends on
  // an unseeded engine.
  CLHEP::HepRandom::setTheSeed(spec.at("seed").get<long>());

  run_manager->Initialize();
  run_manager->BeamOn(events);

  run_manager.reset();
  G4UImanager::GetUIpointer()->SetCoutDestination(nullptr);
  return result;
}
