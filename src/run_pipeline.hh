#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "dev_run.hh"
#include "hits_writer.hh"

// The whole pipeline from a validated spec to an output folder (docs/SIDECAR.md,
// section 5): simulate, write hits.arrow while the events come out in order, compute
// the OID, write provenance.json and run_summary.json, and move the folder into place
// atomically. `vikshep-sim dev-run --out` calls it today; the `run` command will call
// the same function.

// A failure with a stable code from docs/SIDECAR.md, section 4.
class OutputError : public std::runtime_error {
 public:
  OutputError(std::string code, const std::string& message)
      : std::runtime_error(message), code_(std::move(code))
  {
  }
  const std::string& code() const { return code_; }

 private:
  std::string code_;
};

struct PipelineOptions {
  int threads = 1;
  // Also receives every event's hits, in event order (for example the experimental
  // digests). May be empty.
  EventConsumer extra_consumer;
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
};

// spec: a spec that passed validate_spec(). out_dir must not exist and its parent must.
//
// The output is written to a temporary folder next to out_dir
// (".<name>.partial-<process id>") and renamed to out_dir when everything is complete.
// If anything fails, the temporary folder is removed and out_dir does not exist.
// Throws OutputError with code OUTPUT_EXISTS or OUTPUT_NOT_WRITABLE, or
// std::runtime_error for any other failure.
PipelineResult run_to_folder(const nlohmann::json& spec, const std::string& out_dir,
                             const PipelineOptions& options);
