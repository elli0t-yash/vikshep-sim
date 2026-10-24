#include "dev_run.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "G4Box.hh"
#include "G4EmParameters.hh"
#include "G4Exception.hh"
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
#include "G4StepPoint.hh"
#include "G4Track.hh"
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
#include "errors.hh"
#include "event_seed.hh"
#include "geant4_errors.hh"
#include "reorder_buffer.hh"
#include "stderr_session.hh"

using nlohmann::json;

namespace {

// Test only: VIKSHEP_TEST_G4_FATAL=event:N raises a fatal G4Exception when event N is generated (on
// a worker thread), VIKSHEP_TEST_G4_FATAL=construct raises one while the geometry is built (on the
// master thread). Used to test that a fatal Geant4 exception becomes an error line (tests/).
bool test_fatal_wanted(const std::string& what)
{
  const char* value = std::getenv("VIKSHEP_TEST_G4_FATAL");
  return value != nullptr && what == value;
}

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
  // Completed events go here and come out in event_id order.
  ReorderBuffer<EventHits>* ordered = nullptr;
  const CancelCheck* cancel = nullptr;
  std::mutex error_mutex;
  std::exception_ptr error;  // first exception thrown by the consumer
};

// Energy deposited in the event being processed by this thread.
std::vector<double>& thread_event_buffer()
{
  static thread_local std::vector<double> buffer;
  return buffer;
}

// Hits of the event being processed by this thread, in step order.
std::vector<Hit>& thread_event_hits()
{
  static thread_local std::vector<Hit> hits;
  return hits;
}

class DetectorConstruction : public G4VUserDetectorConstruction {
 public:
  DetectorConstruction(const json& spec, const CalorimeterGeometry& geometry,
                       const std::array<bool, 2>& record_hits)
      : geometry_(geometry),
        record_hits_(record_hits),
        absorber_material_(spec.at("geometry").at("absorber").at("material").get<std::string>()),
        active_material_(spec.at("geometry").at("active").at("material").get<std::string>())
  {
  }

  G4VPhysicalVolume* Construct() override
  {
    if (test_fatal_wanted("construct")) {
      G4Exception("vikshep-test", "TEST0001", FatalException, "deliberate failure while building the geometry");
    }
    auto* nist = G4NistManager::Instance();
    G4Material* vacuum = nist->FindOrBuildMaterial("G4_Galactic");
    G4Material* absorber = nist->FindOrBuildMaterial(absorber_material_);
    G4Material* active = nist->FindOrBuildMaterial(active_material_);
    if (vacuum == nullptr || absorber == nullptr || active == nullptr) {
      throw GeometryError("a material of the spec is unknown to Geant4");
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
  std::array<bool, 2> record_hits_;  // per slab type: is it a sensitive volume of the spec
  std::string absorber_material_;
  std::string active_material_;
  G4LogicalVolume* absorber_lv_ = nullptr;
  G4LogicalVolume* active_lv_ = nullptr;
};

// Scores the steps in one slab type. Every step with edep > 0 is added to this
// thread's energy table (both slab types, whatever the spec's `sensitive`, because
// dev-run reports both). If the slab type is a sensitive volume of the spec, the
// step is also recorded as a hit (docs/HITS.md: a hit is a step in a sensitive
// volume with edep > 0).
class SlabDetector : public G4VSensitiveDetector {
 public:
  SlabDetector(const std::string& name, int slab, int layers, bool record_hits)
      : G4VSensitiveDetector(name), slab_(slab), layers_(layers), record_hits_(record_hits)
  {
  }

  G4bool ProcessHits(G4Step* step, G4TouchableHistory*) override
  {
    const double edep = step->GetTotalEnergyDeposit();
    if (edep <= 0.0) {
      return false;
    }
    const G4StepPoint* pre = step->GetPreStepPoint();
    const int copy_no = pre->GetTouchable()->GetCopyNumber();
    thread_event_buffer()[static_cast<std::size_t>(slab_) * layers_ + copy_no] += edep / MeV;

    if (record_hits_) {
      const G4ThreeVector middle = 0.5 * (pre->GetPosition() + step->GetPostStepPoint()->GetPosition());
      const G4ThreeVector momentum = pre->GetMomentum();
      const G4Track* track = step->GetTrack();
      ::Hit hit;  // (G4VSensitiveDetector has a member function called Hit)
      hit.volume = slab_;
      hit.copy_no = copy_no;
      hit.layer = copy_no;  // layered template: the layer index is the copy number
      hit.x_mm = middle.x() / mm;
      hit.y_mm = middle.y() / mm;
      hit.z_mm = middle.z() / mm;
      hit.t_ns = pre->GetGlobalTime() / ns;
      hit.edep_mev = edep / MeV;
      hit.px_mev = momentum.x() / MeV;
      hit.py_mev = momentum.y() / MeV;
      hit.pz_mev = momentum.z() / MeV;
      hit.track_id = track->GetTrackID();
      hit.parent_id = track->GetParentID();
      hit.pdg = track->GetDefinition()->GetPDGEncoding();
      hit.step = track->GetCurrentStepNumber();
      thread_event_hits().push_back(hit);
    }
    return true;
  }

 private:
  int slab_;
  int layers_;
  bool record_hits_;
};

void DetectorConstruction::ConstructSDandField()
{
  auto* manager = G4SDManager::GetSDMpointer();
  auto* absorber_sd = new SlabDetector("absorber_sd", 0, geometry_.layers, record_hits_[0]);
  auto* active_sd = new SlabDetector("active_sd", 1, geometry_.layers, record_hits_[1]);
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
  PrimaryGenerator(const GunConfig& config, std::uint64_t spec_seed, const AfterSeeding& after_seeding)
      : gun_(1), spec_seed_(spec_seed), after_seeding_(after_seeding)
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

  void GeneratePrimaries(G4Event* event) override
  {
    // The random stream of an event depends only on (spec seed, global event
    // id) (docs/HITS.md, section 4.2). This is the first thing that happens for
    // an event that can use random numbers, so reseeding here comes before any
    // draw, including draws by a gun. Geant4 has already seeded this thread's
    // engine from its own queue; this overwrites that, so the requirement holds
    // by construction and does not rest on how G4MTRunManager hands out seeds.
    const std::uint64_t event_id = static_cast<std::uint64_t>(event->GetEventID());
    seed_event_engine(spec_seed_, event_id);
    if (test_fatal_wanted("event:" + std::to_string(event_id))) {
      G4Exception("vikshep-test", "TEST0002", FatalException, "deliberate failure while generating an event");
    }
    if (after_seeding_) {
      after_seeding_(event_id);  // test hook
    }
    gun_.GeneratePrimaryVertex(event);
  }

 private:
  G4ParticleGun gun_;
  std::uint64_t spec_seed_;
  AfterSeeding after_seeding_;
};

class EventAction : public G4UserEventAction {
 public:
  explicit EventAction(SharedResults* shared) : shared_(shared) {}

  void BeginOfEventAction(const G4Event*) override
  {
    thread_event_buffer().assign(2 * static_cast<std::size_t>(shared_->geometry.layers), 0.0);
    thread_event_hits().clear();
  }

  void EndOfEventAction(const G4Event* event) override
  {
    const std::size_t size = 2 * static_cast<std::size_t>(shared_->geometry.layers);
    const std::vector<double>& buffer = thread_event_buffer();
    std::copy(buffer.begin(), buffer.end(),
              shared_->edep_mev->begin() + static_cast<std::ptrdiff_t>(event->GetEventID() * size));

    // Hand the event to the reorder buffer. An exception must not travel through
    // Geant4: keep the first one, finish the run, rethrow it afterwards.
    EventHits finished;
    finished.event_id = static_cast<std::uint64_t>(event->GetEventID());
    finished.hits = std::move(thread_event_hits());
    thread_event_hits().clear();
    try {
      shared_->ordered->submit(finished.event_id, std::move(finished));
    }
    catch (...) {
      std::lock_guard<std::mutex> lock(shared_->error_mutex);
      if (!shared_->error) {
        shared_->error = std::current_exception();
      }
    }

    // Soft abort: this thread's run manager lets the current event finish (this one has)
    // and starts no other. Every worker polls the flag after each of its events, so every
    // worker stops after the event it was processing when the flag was set.
    if (shared_->cancel != nullptr && *shared_->cancel && (*shared_->cancel)()) {
      G4RunManager::GetRunManager()->AbortRun(true);
    }
  }

 private:
  SharedResults* shared_;
};

class Actions : public G4VUserActionInitialization {
 public:
  Actions(const GunConfig& gun, SharedResults* shared, const AfterSeeding& after_seeding)
      : gun_(gun), shared_(shared), after_seeding_(after_seeding)
  {
  }

  void Build() const override
  {
    install_exception_handler_on_this_thread();  // this is a worker thread
    SetUserAction(new PrimaryGenerator(gun_, shared_->spec_seed, after_seeding_));
    SetUserAction(new EventAction(shared_));
  }

 private:
  GunConfig gun_;
  SharedResults* shared_;
  AfterSeeding after_seeding_;
};

}  // namespace

DevRunResult run_dev_simulation(const json& spec, int threads, const EventConsumer& consumer,
                                const AfterSeeding& after_seeding, const CancelCheck& cancel)
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

  install_exception_handler_on_this_thread();  // the master thread (no-op unless `run` enabled it)

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

  // Sensitive volumes of the spec: which slab types record hits.
  std::array<bool, 2> record_hits = {false, false};
  for (const json& name : spec.at("sensitive")) {
    for (std::size_t i = 0; i < 2; ++i) {
      if (name == kSlabNames[i]) {
        record_hits[i] = true;
      }
    }
  }

  // Events come out of the buffer in event_id order whatever the thread count.
  // The window bounds memory (see reorder_buffer.hh): Geant4's run manager
  // starts events in increasing id order, which is what makes it safe.
  ReorderBuffer<EventHits> ordered(
      [&consumer](std::uint64_t, EventHits&& event) {
        if (consumer) {
          consumer(std::move(event));
        }
      },
      0, static_cast<std::size_t>(16 * threads + 16));

  SharedResults shared;
  shared.ordered = &ordered;
  shared.cancel = &cancel;
  shared.geometry = geometry;
  shared.spec_seed = spec.at("seed").get<std::uint64_t>();
  shared.edep_mev = &result.edep_mev;

  run_manager->SetUserInitialization(new DetectorConstruction(spec, geometry, record_hits));
  run_manager->SetUserInitialization(physics);
  run_manager->SetUserInitialization(new Actions(gun, &shared, after_seeding));

  // Geant4's master engine still seeds Geant4's own per-event seeds, which
  // EventAction replaces. Seed it from the spec anyway so nothing depends on
  // an unseeded engine.
  CLHEP::HepRandom::setTheSeed(spec.at("seed").get<long>());

  run_manager->Initialize();
  // A cancellation that arrived while Geant4 was initialising: do not start the run.
  if (!(cancel && cancel())) {
    run_manager->BeamOn(events);
  }

  run_manager.reset();
  G4UImanager::GetUIpointer()->SetCoutDestination(nullptr);
  if (shared.error) {
    std::rethrow_exception(shared.error);
  }
  const bool cancelled = cancel && cancel();
  if (!cancelled &&
      (ordered.next_id() != static_cast<std::uint64_t>(events) || ordered.pending() != 0)) {
    throw std::runtime_error("not every event was delivered in order");
  }
  return result;
}
