// `vikshep-sim run`: the command of the sidecar contract (docs/SIDECAR.md, section 1.3).

#include <iostream>
#include <memory>
#include <string>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "cancellation.hh"
#include "commands.hh"
#include "errors.hh"
#include "failure.hh"
#include "geant4_errors.hh"
#include "progress.hh"
#include "run_pipeline.hh"
#include "spec_file.hh"
#include "stdout_channel.hh"

namespace {

using nlohmann::json;

// Everything that goes to the machine-readable stdout goes through this one object, so
// lines are written from one place (and StdoutChannel writes each with a single call).
class Emitter {
 public:
  explicit Emitter(bool enabled) : enabled_(enabled) {}

  void progress(std::uint64_t done, std::uint64_t total)
  {
    emit({{"done", done}, {"event", "progress"}, {"total", total}});
  }
  void done(const std::string& oid) { emit({{"event", "done"}, {"oid", oid}}); }
  void error(const json& line) { emit(line); }

 private:
  void emit(const json& line)
  {
    if (enabled_) {
      StdoutChannel::write_line(line.dump());
    }
  }
  bool enabled_;
};

int run_run(const std::string& spec_path, const std::string& out_dir, int threads, bool jsonl)
{
  // First of all: take stdout for the channel, so nothing else can reach it.
  StdoutChannel::capture();
  cancellation::install_handlers();
  enable_run_exception_handler(jsonl);  // a fatal Geant4 exception becomes SIMULATION_FAILED, exit 1
  Emitter emitter(jsonl);

  try {
    // 1. Validation, in full, before anything else (docs/SIDECAR.md, section 6).
    const SpecLoad load = load_valid_spec(spec_path);
    if (!load.ok) {
      emitter.error(load.error_line);
      std::cerr << load.human << "\n";
      return exit_code_for(load.error_line.at("code").get<std::string>());
    }

    // 2. The run. Data sets and the output folder are checked inside, before any output exists.
    ProgressThrottle throttle(static_cast<std::uint64_t>(load.spec.at("events").get<std::int64_t>()));
    PipelineOptions options;
    options.threads = threads;
    options.cancel = [] { return cancellation::requested(); };
    options.on_progress = [&](std::uint64_t done, std::uint64_t total) {
      if (throttle.should_emit(done)) {
        emitter.progress(done, total);
      }
    };
    const PipelineResult result = run_to_folder(load.spec, out_dir, options);

    // 3. The folder is in place: now, and only now, the done line.
    emitter.done(result.oid);
    return kExitOk;
  }
  catch (...) {
    const Failure failure = classify_current_exception();
    emitter.error({{"code", failure.code}, {"event", "error"}, {"message", failure.message}});
    std::cerr << "vikshep-sim run: " << failure.code << ": " << failure.detail << "\n";
    return exit_code_for(failure.code);
  }
}

}  // namespace

void register_run(CLI::App& app, int& exit_code)
{
  CLI::App* sub = app.add_subcommand("run", "Run a simulation from a spec and write the output folder");
  auto spec = std::make_shared<std::string>();
  auto out = std::make_shared<std::string>();
  auto threads = std::make_shared<int>(1);
  auto progress = std::make_shared<std::string>("none");
  sub->add_option("--spec", *spec, "Path to the JSON spec")->required();
  sub->add_option("--out", *out,
                  "Output folder to create. It must not exist, its parent must; it is written "
                  "atomically")
      ->required();
  sub->add_option("--threads", *threads, "Worker threads (default 1). Never changes the output")
      ->check(CLI::PositiveNumber);
  sub->add_option("--progress", *progress, "'jsonl': progress and result as JSON lines on stdout; 'none' (default): stdout stays empty")
      ->check(CLI::IsMember({"jsonl", "none"}));
  sub->footer(
      "Output: hits.arrow, provenance.json, run_summary.json in --out.\n"
      "With --progress jsonl stdout carries only JSON lines (progress, then done with the OID, or one\n"
      "error line); everything else, including Geant4's output, goes to stderr.\n"
      "Exit codes: 0 ok; 1 simulation or internal error; 2 bad usage, invalid spec or --out exists;\n"
      "3 Geant4 data sets missing; 4 cancelled (SIGTERM or SIGINT; a second signal exits 4 at once).\n"
      "Error codes: USAGE, SPEC_INVALID, SPEC_UNREADABLE, SPEC_NOT_JSON, SPEC_VERSION_UNSUPPORTED,\n"
      "OUTPUT_EXISTS, DATASETS_MISSING, GEOMETRY_ERROR, OUTPUT_NOT_WRITABLE, SIMULATION_FAILED,\n"
      "INTERNAL, CANCELLED.");
  sub->callback([spec, out, threads, progress, &exit_code]() {
    exit_code = run_run(*spec, *out, *threads, *progress == "jsonl");
  });
}
