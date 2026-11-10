#include "run_pipeline.hh"

#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <fstream>
#include <system_error>
#include <vector>
#include <cstdint>
#include <string>
#include <utility>

#include "atomic_rename.hh"
#include "datasets_check.hh"
#include "geant4_errors.hh"
#include "run_outputs.hh"
#include "spec_canonical.hh"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// Removes the temporary folder unless released.
class FolderGuard {
 public:
  explicit FolderGuard(fs::path folder) : folder_(std::move(folder))
  {
    set_cleanup_folder(folder_.string());  // so a fatal Geant4 exception can remove it too
  }
  ~FolderGuard()
  {
    clear_cleanup_folder();
    if (active_) {
      std::error_code ec;
      fs::remove_all(folder_, ec);  // best effort: nothing more can be done in a destructor
    }
  }
  void release() { active_ = false; }
  FolderGuard(const FolderGuard&) = delete;
  FolderGuard& operator=(const FolderGuard&) = delete;

 private:
  fs::path folder_;
  bool active_ = true;
};

void write_text_file(const fs::path& path, const std::string& text)
{
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  out.flush();
  if (!out) {
    throw OutputError("OUTPUT_NOT_WRITABLE", "cannot write " + path.filename().string());
  }
}

}  // namespace

PipelineResult run_to_folder(const json& user_spec, const std::string& out_dir,
                             const PipelineOptions& options)
{
  const auto started = std::chrono::steady_clock::now();
  const json spec = resolve_spec(user_spec);

  // --- the target: must not exist, its parent must
  fs::path target = fs::path(out_dir).lexically_normal();
  if (!target.has_filename()) {
    target = target.parent_path();  // "dir/" -> "dir"
  }
  std::error_code ec;
  if (fs::symlink_status(target, ec).type() != fs::file_type::not_found) {
    throw OutputError("OUTPUT_EXISTS", "the output folder already exists");
  }
  fs::path parent = target.parent_path();
  if (parent.empty()) {
    parent = ".";
  }
  if (!fs::is_directory(parent, ec)) {
    throw OutputError("OUTPUT_EXISTS", "the parent of the output folder does not exist");
  }

  // --- the Geant4 data sets the physics list needs, checked before anything is created
  const auto missing_sets =
      find_missing_data_sets(required_data_sets(spec.at("physics").at("list").get<std::string>()));
  if (!missing_sets.empty()) {
    throw OutputError("DATASETS_MISSING", describe_missing(missing_sets));
  }

  // --- the temporary folder, next to the target so that the rename stays on one file system
  const fs::path temp =
      parent / ("." + target.filename().string() + ".partial-" + std::to_string(::getpid()));
  fs::remove_all(temp, ec);  // a leftover of an earlier process with the same id
  if (!fs::create_directory(temp, ec) || ec) {
    throw OutputError("OUTPUT_NOT_WRITABLE", "cannot create the temporary output folder");
  }
  FolderGuard guard(temp);

  // --- simulate, writing the hits as the events come out in order
  const std::vector<std::string> volumes = spec.at("sensitive").get<std::vector<std::string>>();
  HitsWriter writer((temp / "hits.arrow").string(), volumes, options.batch_rows);

  // The 3-D export, if asked for: trajectories.json is written event by event from the reorder
  // buffer's output, so it is in event_id order whatever the thread count.
  std::unique_ptr<TrajectoryWriter> trajectories;
  std::string spec_oid;
  if (options.export3d.enabled) {
    spec_oid = spec_oid_of(spec);
    trajectories = std::make_unique<TrajectoryWriter>((temp / kTrajectoriesFileName).string(), spec_oid,
                                                      options.export3d);
  }

  PipelineResult result;
  std::uint64_t completed = 0;
  const EventConsumer consumer = [&](EventHits&& event) {
    ++completed;
    if (trajectories && event.trajectories) {
      trajectories->add_event(event.event_id, *event.trajectories);
      ++result.export_events;
    }
    if (event.hits.empty()) {
      ++result.events_without_hits;
    }
    else {
      ++result.events_with_hits;
    }
    writer.add_event(event);
    if (options.extra_consumer) {
      options.extra_consumer(std::move(event));
    }
  };
  const std::uint64_t total = static_cast<std::uint64_t>(spec.at("events").get<std::int64_t>());
  const EventConsumer reporting_consumer = [&](EventHits&& event) {
    consumer(std::move(event));
    if (options.on_progress) {
      options.on_progress(completed, total);
    }
  };
  if (options.on_progress) {
    options.on_progress(0, total);
  }
  result.table = run_dev_simulation(spec, options.threads, reporting_consumer, nullptr, options.cancel,
                                    options.export3d);
  if (options.cancel && options.cancel()) {
    throw OutputError("CANCELLED", "the run was cancelled");  // the guard removes the temporary folder
  }
  result.hits_file = writer.finish();
  result.oid = result.hits_file.oid;
  if (options.export3d.enabled) {
    result.exports.push_back(write_geometry_glb((temp / kGeometryFileName).string(), spec));
    result.exports.push_back(trajectories->finish());
  }

  const auto finished = std::chrono::steady_clock::now();
  result.wall_time_s = std::chrono::duration<double>(finished - started).count();

  // --- provenance.json and run_summary.json
  ProvenanceFacts provenance;
  provenance.run_manager = result.table.run_manager_type;
  provenance.default_cut_mm = result.table.default_cut_mm;
  const auto required = required_data_sets(result.table.physics_list);
  provenance.data_sets = describe_data_sets(required);
  provenance.geant4_conda = describe_geant4_package(required);
  json exports_json = json::array();
  for (const ExportedFile& f : result.exports) {
    exports_json.push_back(exported_file_json(f));
  }
  if (options.export3d.enabled) {
    provenance.export_3d = {{"events", result.export_events},
                            {"files", exports_json},
                            {"format_version", kTrajectoriesFormatVersion},
                            {"max_interactions_per_event", kExportMaxInteractionsPerEvent},
                            {"max_tracks_per_event", kExportMaxTracksPerEvent},
                            {"min_energy_GeV", options.export3d.min_energy_gev}};
  }
  write_text_file(temp / "provenance.json", to_file_text(make_provenance(spec, provenance)));

  RunSummaryFacts summary;
  summary.oid = result.oid;
  summary.spec_version = spec.at("spec_version").get<std::string>();
  summary.events_requested = static_cast<std::uint64_t>(result.table.events);
  summary.events_completed = completed;
  summary.events_with_hits = result.events_with_hits;
  summary.events_without_hits = result.events_without_hits;
  summary.hits_total = result.hits_file.rows;
  summary.hits_file_bytes = result.hits_file.bytes;
  summary.threads = options.threads;
  summary.wall_time_s = result.wall_time_s;
  if (options.export3d.enabled) {
    summary.exports = exports_json;
  }
  write_text_file(temp / "run_summary.json", to_file_text(make_run_summary(summary)));

  // A cancellation that came during the last steps still counts: nothing is in place yet.
  if (options.cancel && options.cancel()) {
    throw OutputError("CANCELLED", "the run was cancelled");
  }

  // --- into place. The rename fails if the target exists (an atomic no-overwrite call
  // where the operating system has one, see atomic_rename.hh), so another writer's folder
  // is never replaced.
  rename_no_replace(temp, target, ec);
  if (ec == std::errc::file_exists) {
    throw OutputError("OUTPUT_EXISTS", "the output folder appeared while the run was in progress");
  }
  if (ec) {
    throw OutputError("OUTPUT_NOT_WRITABLE", "cannot move the output folder into place: " + ec.message());
  }
  guard.release();
  return result;
}
