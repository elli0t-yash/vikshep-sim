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

#include "G4AutoDelete.hh"
#include "G4Box.hh"
#include "G4ChordFinder.hh"
#include "G4ExactHelixStepper.hh"
#include "G4FieldManager.hh"
#include "G4Mag_UsualEqRhs.hh"
#include "G4UniformMagField.hh"
#include "G4Tubs.hh"
#include "G4TransportationManager.hh"
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
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
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
#include "tracker_geometry.hh"
#include "versions.hh"

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
  std::size_t event_buffer_size = 0;  // slabs * layers: size of the energy table of one event
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

// The uniform magnetic field of the spec (docs/SPEC.md, 2.6), in tesla, in the global frame.
struct FieldConfig {
  bool enabled = false;  // false when the vector is zero: then no Geant4 field object is created
  double bx_t = 0.0;
  double by_t = 0.0;
  double bz_t = 0.0;
};

FieldConfig field_config_of(const json& spec)
{
  FieldConfig f;
  if (!spec.contains("field")) {
    return f;  // a spec written as 1.0 has no field block
  }
  const json& b = spec.at("field").at("B_T");
  f.bx_t = b[0].get<double>();
  f.by_t = b[1].get<double>();
  f.bz_t = b[2].get<double>();
  f.enabled = f.bx_t != 0.0 || f.by_t != 0.0 || f.bz_t != 0.0;
  return f;
}

// Installs the field for the thread that calls it (Geant4 field managers are thread-local, so
// this runs from ConstructSDandField, which every worker thread calls). Nothing at all is
// created for a zero field. The stepper is the exact helix of a charged particle in a uniform
// field, so the integrator adds no error of its own (docs/SPEC.md, 2.6); the accuracy
// parameters stay at Geant4's defaults.
void install_uniform_field(const FieldConfig& config)
{
  if (!config.enabled) {
    return;
  }
  auto* field = new G4UniformMagField(G4ThreeVector(config.bx_t, config.by_t, config.bz_t) * tesla);
  G4AutoDelete::Register(field);
  auto* equation = new G4Mag_UsualEqRhs(field);
  G4AutoDelete::Register(equation);
  auto* stepper = new G4ExactHelixStepper(equation);
  auto* chord_finder = new G4ChordFinder(field, 1e-2 * mm, stepper);  // 0.01 mm: Geant4's default minimum step
  G4AutoDelete::Register(chord_finder);
  G4FieldManager* manager = G4TransportationManager::GetTransportationManager()->GetFieldManager();
  manager->SetDetectorField(field);
  manager->SetChordFinder(chord_finder);
}

class DetectorConstruction : public G4VUserDetectorConstruction {
 public:
  DetectorConstruction(const json& spec, const CalorimeterGeometry& geometry,
                       const std::array<bool, 2>& record_hits, const FieldConfig& field)
      : geometry_(geometry),
        record_hits_(record_hits),
        field_(field),
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
  FieldConfig field_;
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
  // slab: row of the energy table; volume: index into kVolumeNames of the hits it records.
  SlabDetector(const std::string& name, int slab, int volume, int layers, bool record_hits)
      : G4VSensitiveDetector(name),
        slab_(slab),
        volume_(volume),
        layers_(layers),
        record_hits_(record_hits)
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
      hit.volume = volume_;
      hit.copy_no = copy_no;
      hit.layer = copy_no;  // both templates: the layer index is the copy number
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
  int volume_;
  int layers_;
  bool record_hits_;
};

void DetectorConstruction::ConstructSDandField()
{
  auto* manager = G4SDManager::GetSDMpointer();
  auto* absorber_sd =
      new SlabDetector("absorber_sd", 0, kVolumeAbsorber, geometry_.layers, record_hits_[0]);
  auto* active_sd = new SlabDetector("active_sd", 1, kVolumeActive, geometry_.layers, record_hits_[1]);
  manager->AddNewDetector(absorber_sd);
  manager->AddNewDetector(active_sd);
  SetSensitiveDetector("absorber", absorber_sd);
  SetSensitiveDetector("active", active_sd);
  install_uniform_field(field_);
}

// The tracker_barrel template (docs/SPEC.md, 2.5): concentric silicon tubes around the z axis, in
// a box of vacuum. Every layer is a sensitive volume named "tracker_layer", copy number = layer
// index (0 = innermost).
class TrackerConstruction : public G4VUserDetectorConstruction {
 public:
  TrackerConstruction(const TrackerGeometry& geometry, const FieldConfig& field, bool record_hits)
      : geometry_(geometry), field_(field), record_hits_(record_hits)
  {
  }

  G4VPhysicalVolume* Construct() override
  {
    if (test_fatal_wanted("construct")) {
      G4Exception("vikshep-test", "TEST0001", FatalException, "deliberate failure while building the geometry");
    }
    auto* nist = G4NistManager::Instance();
    G4Material* vacuum = nist->FindOrBuildMaterial("G4_Galactic");
    if (vacuum == nullptr) {
      throw GeometryError("a material of the spec is unknown to Geant4");
    }
    auto* world_solid = new G4Box("world", 0.5 * geometry_.world_transverse_mm() * mm,
                                  0.5 * geometry_.world_transverse_mm() * mm,
                                  0.5 * geometry_.world_length_mm() * mm);
    auto* world_lv = new G4LogicalVolume(world_solid, vacuum, "world");
    auto* world_pv = new G4PVPlacement(nullptr, G4ThreeVector(), world_lv, "world", nullptr, false,
                                       0, false);
    for (std::size_t i = 0; i < geometry_.layers.size(); ++i) {
      const TrackerLayer& layer = geometry_.layers[i];
      G4Material* material = nist->FindOrBuildMaterial(layer.material);
      if (material == nullptr) {
        throw GeometryError("a material of the spec is unknown to Geant4");
      }
      // Logical volumes need distinct names for the name-based sensitive detector assignment
      // below; the physical volume keeps the one documented name (the one hits.arrow shows).
      const std::string lv_name = logical_name(i);
      auto* tube = new G4Tubs(lv_name, layer.inner_radius_mm() * mm, layer.outer_radius_mm() * mm,
                              layer.half_length_mm * mm, 0.0, CLHEP::twopi);
      auto* lv = new G4LogicalVolume(tube, material, lv_name);
      new G4PVPlacement(nullptr, G4ThreeVector(), lv, kTrackerVolumeName, world_lv, false,
                        static_cast<int>(i), false);
    }
    return world_pv;
  }

  void ConstructSDandField() override
  {
    auto* detector = new SlabDetector("tracker_sd", 0, kVolumeTrackerLayer,
                                      static_cast<int>(geometry_.layers.size()), record_hits_);
    G4SDManager::GetSDMpointer()->AddNewDetector(detector);
    for (std::size_t i = 0; i < geometry_.layers.size(); ++i) {
      SetSensitiveDetector(logical_name(i), detector);
    }
    install_uniform_field(field_);
  }

 private:
  static std::string logical_name(std::size_t layer)
  {
    return std::string(kTrackerVolumeName) + "_" + std::to_string(layer);
  }

  TrackerGeometry geometry_;
  FieldConfig field_;
  bool record_hits_;
};

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

// The tracks of the event being processed by this thread, for the 3-D export (docs/EXPORT3D.md).
// Only filled for the first events, and only when the export is on.
struct TrackCollector {
  bool active = false;
  std::uint32_t total = 0;             // every track Geant4 created in the event
  TrackSelector selector;              // at most kExportMaxTracksPerEvent of the primaries and the tracks at
                                       // or above the minimum energy, chosen while the event runs
  bool collecting = false;             // the track being tracked is stored in `selector`
};

TrackCollector& thread_tracks()
{
  static thread_local TrackCollector collector;
  return collector;
}

// Read-only: looks at a track when it starts and when it ends, changes nothing.
class ExportTrackingAction : public G4UserTrackingAction {
 public:
  explicit ExportTrackingAction(double min_energy_gev) : min_energy_gev_(min_energy_gev) {}

  void PreUserTrackingAction(const G4Track* track) override
  {
    TrackCollector& c = thread_tracks();
    if (!c.active) {
      return;
    }
    ++c.total;
    const double energy_gev = track->GetKineticEnergy() / GeV;
    const bool primary = track->GetParentID() == 0;
    if (!primary && !(energy_gev >= min_energy_gev_)) {
      c.collecting = false;
      return;
    }
    TrackRecord record;
    record.track_id = track->GetTrackID();
    record.parent_id = track->GetParentID();
    record.pdg = track->GetDefinition()->GetPDGEncoding();
    record.charge = static_cast<std::int32_t>(std::lround(track->GetDefinition()->GetPDGCharge()));
    record.kinetic_energy_start_gev = energy_gev;
    const G4ThreeVector start = track->GetPosition();
    record.points.push_back({start.x() / mm, start.y() / mm, start.z() / mm});
    c.collecting = c.selector.begin_track(std::move(record));
  }

  void PostUserTrackingAction(const G4Track*) override { thread_tracks().collecting = false; }

 private:
  double min_energy_gev_;
};

// Read-only: records where each step of a collected track ended.
class ExportSteppingAction : public G4UserSteppingAction {
 public:
  void UserSteppingAction(const G4Step* step) override
  {
    TrackCollector& c = thread_tracks();
    if (!c.active || !c.collecting) {
      return;
    }
    const G4ThreeVector p = step->GetPostStepPoint()->GetPosition();
    c.selector.add_point({p.x() / mm, p.y() / mm, p.z() / mm});
  }
};

class EventAction : public G4UserEventAction {
 public:
  EventAction(SharedResults* shared, const ExportOptions& export3d) : shared_(shared), export_(export3d) {}

  void BeginOfEventAction(const G4Event* event) override
  {
    thread_event_buffer().assign(shared_->event_buffer_size, 0.0);
    thread_event_hits().clear();
    TrackCollector& c = thread_tracks();
    c = TrackCollector();
    c.active = export_.enabled && event->GetEventID() < export_.events;
  }

  void EndOfEventAction(const G4Event* event) override
  {
    const std::size_t size = shared_->event_buffer_size;
    const std::vector<double>& buffer = thread_event_buffer();
    std::copy(buffer.begin(), buffer.end(),
              shared_->edep_mev->begin() + static_cast<std::ptrdiff_t>(event->GetEventID() * size));

    // Hand the event to the reorder buffer. An exception must not travel through
    // Geant4: keep the first one, finish the run, rethrow it afterwards.
    EventHits finished;
    finished.event_id = static_cast<std::uint64_t>(event->GetEventID());
    finished.hits = std::move(thread_event_hits());
    thread_event_hits().clear();
    TrackCollector& collected = thread_tracks();
    if (collected.active) {
      finished.trajectories = collected.selector.finish(collected.total);
      collected = TrackCollector();
    }
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
  ExportOptions export_;
};

class Actions : public G4VUserActionInitialization {
 public:
  Actions(const GunConfig& gun, SharedResults* shared, const AfterSeeding& after_seeding,
          const ExportOptions& export3d)
      : gun_(gun), shared_(shared), after_seeding_(after_seeding), export_(export3d)
  {
  }

  void Build() const override
  {
    install_exception_handler_on_this_thread();  // this is a worker thread
    SetUserAction(new PrimaryGenerator(gun_, shared_->spec_seed, after_seeding_));
    SetUserAction(new EventAction(shared_, export_));
    if (export_.enabled) {
      // Only for the 3-D export, and read-only: without it these actions do not exist.
      SetUserAction(new ExportTrackingAction(export_.min_energy_gev));
      SetUserAction(new ExportSteppingAction());
    }
  }

 private:
  GunConfig gun_;
  SharedResults* shared_;
  AfterSeeding after_seeding_;
  ExportOptions export_;
};

}  // namespace

DevRunResult run_dev_simulation(const json& spec, int threads, const EventConsumer& consumer,
                                const AfterSeeding& after_seeding, const CancelCheck& cancel,
                                const ExportOptions& export3d)
{
  if (threads < 1) {
    throw std::invalid_argument("threads must be at least 1");
  }

  const bool tracker = spec.at("template").get<std::string>() == kTemplateTrackerBarrel;
  CalorimeterGeometry geometry;
  TrackerGeometry tracker_geometry;
  if (tracker) {
    for (const json& l : spec.at("geometry").at("layers")) {
      TrackerLayer layer;
      layer.radius_mm = l.at("radius_mm").get<double>();
      layer.thickness_mm = l.at("thickness_mm").get<double>();
      layer.half_length_mm = l.at("half_length_mm").get<double>();
      layer.material = l.at("material").get<std::string>();
      tracker_geometry.layers.push_back(layer);
    }
  }
  else {
    const json& g = spec.at("geometry");
    geometry.layers = g.at("layers").get<int>();
    geometry.absorber_thickness_mm = g.at("absorber").at("thickness_mm").get<double>();
    geometry.active_thickness_mm = g.at("active").at("thickness_mm").get<double>();
    geometry.transverse_mm = g.at("transverse_mm").get<double>();
  }
  const int layer_count = tracker ? static_cast<int>(tracker_geometry.layers.size()) : geometry.layers;
  const int slabs = tracker ? 1 : 2;
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
  result.slabs = slabs;
  result.layer_count = layer_count;
  result.events = events;
  result.threads = threads;
  result.incident_energy_mev = gun.energy_mev;
  result.particle = gun.particle;
  result.physics_list = spec.at("physics").at("list").get<std::string>();
  result.run_manager_type = kRunManagerTypeName;
  result.edep_mev.assign(static_cast<std::size_t>(events) * slabs * layer_count, 0.0);

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

  // Sensitive volumes of the spec: which volumes record hits.
  std::array<bool, kVolumeCount> record_volume = {false, false, false};
  for (const json& name : spec.at("sensitive")) {
    for (std::size_t i = 0; i < kVolumeCount; ++i) {
      if (name == kVolumeNames[i]) {
        record_volume[i] = true;
      }
    }
  }
  const std::array<bool, 2> record_hits = {record_volume[kVolumeAbsorber], record_volume[kVolumeActive]};
  const FieldConfig field = field_config_of(spec);

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
  shared.event_buffer_size = static_cast<std::size_t>(slabs) * layer_count;
  shared.spec_seed = spec.at("seed").get<std::uint64_t>();
  shared.edep_mev = &result.edep_mev;

  if (tracker) {
    run_manager->SetUserInitialization(
        new TrackerConstruction(tracker_geometry, field, record_volume[kVolumeTrackerLayer]));
  }
  else {
    run_manager->SetUserInitialization(new DetectorConstruction(spec, geometry, record_hits, field));
  }
  run_manager->SetUserInitialization(physics);
  run_manager->SetUserInitialization(new Actions(gun, &shared, after_seeding, export3d));

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
