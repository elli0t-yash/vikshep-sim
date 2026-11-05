#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

#include "dev_run.hh"
#include "errors.hh"
#include "export3d.hh"
#include "hits_writer.hh"

// The whole pipeline from a validated spec to an output folder (docs/SIDECAR.md,
// section 5): simulate, write hits.arrow while the events come out in order, compute
// the OID, write provenance.json and run_summary.json, and move the folder into place
// atomically. `vikshep-sim dev-run --out` calls it today; the `run` command will call
// the same function.

using OutputError = CodedError;

struct PipelineOptions {
  int threads = 1;
  // Also receives every event's hits, in event order (for example the experimental
  // digests). May be empty.
  EventConsumer extra_consumer;
  // Called with (0, total) when the simulation is about to start, and with (n, total) each
  // time the n-th event has been written, in event order. May be empty.
  std::function<void(std::uint64_t done, std::uint64_t total)> on_progress;
  // Polled during the run (see CancelCheck). If it returns true before the output is moved
  // into place, the run stops, nothing is left behind and CodedError("CANCELLED") is thrown.
  // May be empty.
  CancelCheck cancel;
  // The optional 3-D export (docs/EXPORT3D.md). Off by default; when off nothing about it is
  // created or recorded.
  ExportOptions export3d;
  // Test only: rows per record batch of hits.arrow. The default is the one of HITS.md.
  std::size_t batch_rows = HitsWriter::kDefaultBatchRows;
};

struct PipelineResult {
  std::string oid;
  HitsFileSummary hits_file;
  std::uint64_t events_with_hits = 0;
  std::uint64_t events_without_hits = 0;
  double wall_time_s = 0.0;
  DevRunResult table;  // the energy table of the same run (dev-run prints it)
  // With the export: geometry.glb, then trajectories.json. Empty otherwise.
  std::vector<ExportedFile> exports;
  std::uint64_t export_events = 0;  // events in trajectories.json
};

// spec: a spec that passed validate_spec(). out_dir must not exist and its parent must.
//
// The output is written to a temporary folder next to out_dir
// (".<name>.partial-<process id>") and renamed to out_dir when everything is complete.
// If anything fails, the temporary folder is removed and out_dir does not exist.
// Order of checks, before anything is created: the target (OUTPUT_EXISTS), then the Geant4
// data sets the physics list needs (DATASETS_MISSING). Throws CodedError (OutputError) with
// code OUTPUT_EXISTS, DATASETS_MISSING, OUTPUT_NOT_WRITABLE or CANCELLED; WriteError,
// GeometryError, IntegrityError (errors.hh) or std::runtime_error for other failures.
PipelineResult run_to_folder(const nlohmann::json& spec, const std::string& out_dir,
                             const PipelineOptions& options);
