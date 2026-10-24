// EXPERIMENTAL. Not part of the sidecar contract (docs/SIDECAR.md). To be
// replaced by `run`.

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "commands.hh"
#include "datasets_check.hh"
#include "dev_run.hh"
#include "event_digest.hh"
#include "run_pipeline.hh"
#include "spec_canonical.hh"
#include "spec_file.hh"

namespace {

// Mean and standard error of the mean of the values.
struct Mean {
  double mean = 0.0;
  double error = 0.0;
};

Mean mean_of(const std::vector<double>& values)
{
  Mean m;
  const double n = static_cast<double>(values.size());
  for (double v : values) {
    m.mean += v;
  }
  m.mean /= n;
  if (values.size() > 1) {
    double sum_sq = 0.0;
    for (double v : values) {
      sum_sq += (v - m.mean) * (v - m.mean);
    }
    m.error = std::sqrt(sum_sq / (n - 1.0) / n);
  }
  return m;
}

void print_report(const DevRunResult& r)
{
  const int layers = r.geometry.layers;
  std::cout << std::fixed;
  std::cout << "EXPERIMENTAL dev-run (outside the sidecar contract)\n";
  std::cout << "particle " << r.particle << ", " << std::setprecision(1) << r.incident_energy_mev
            << " MeV, " << r.events << " events, physics list " << r.physics_list << "\n";
  std::cout << "run manager " << r.run_manager_type << ", threads " << r.threads
            << ", default production cut " << std::setprecision(3) << r.default_cut_mm << " mm\n\n";

  std::cout << "Mean energy deposited per event, by layer\n";
  std::cout << std::setw(6) << "layer" << std::setw(18) << "absorber_MeV" << std::setw(18)
            << "active_MeV\n";
  std::cout << std::setprecision(3);
  for (int layer = 0; layer < layers; ++layer) {
    double absorber = 0.0;
    double active = 0.0;
    for (int e = 0; e < r.events; ++e) {
      absorber += r.edep(e, 0, layer);
      active += r.edep(e, 1, layer);
    }
    std::cout << std::setw(6) << layer << std::setw(18) << absorber / r.events << std::setw(18)
              << active / r.events << "\n";
  }

  std::vector<double> absorber_total(r.events, 0.0);
  std::vector<double> active_total(r.events, 0.0);
  std::vector<double> total(r.events, 0.0);
  for (int e = 0; e < r.events; ++e) {
    for (int layer = 0; layer < layers; ++layer) {
      absorber_total[e] += r.edep(e, 0, layer);
      active_total[e] += r.edep(e, 1, layer);
    }
    total[e] = absorber_total[e] + active_total[e];
  }
  const Mean a = mean_of(absorber_total);
  const Mean g = mean_of(active_total);
  const Mean t = mean_of(total);
  std::cout << "\nMean total over events (+- standard error of the mean)\n";
  std::cout << "  absorber_MeV  " << a.mean << " +- " << a.error << "\n";
  std::cout << "  active_MeV    " << g.mean << " +- " << g.error << "\n";
  std::cout << "  total_MeV     " << t.mean << " +- " << t.error << "\n";
  std::cout << "  fraction of incident energy  " << std::setprecision(4)
            << t.mean / r.incident_energy_mev << " +- " << t.error / r.incident_energy_mev
            << "\n";
}

int run_dev(const std::string& path, int threads, const std::string& digests_path,
            const std::string& out_path)
{
  const SpecLoad load = load_valid_spec(path);
  if (!load.ok) {
    std::cout << load.error_line.dump() << "\n";
    std::cerr << load.human << "\n";
    return kExitUsageOrSpec;
  }
  const nlohmann::json spec = resolve_spec(load.spec);

  const auto missing = find_missing_data_sets(
      required_data_sets(spec.at("physics").at("list").get<std::string>()));
  if (!missing.empty()) {
    std::cerr << describe_missing(missing) << "\n";
    return kExitDataSetsMissing;
  }

  try {
    // Optional: one JSON line per event, in event order (EXPERIMENTAL).
    std::ofstream digests;
    EventConsumer consumer;
    if (!digests_path.empty()) {
      digests.open(digests_path, std::ios::binary | std::ios::trunc);
      if (!digests) {
        std::cerr << "cannot write " << digests_path << "\n";
        return kExitFailure;
      }
      const std::vector<std::string> volumes = spec.at("sensitive").get<std::vector<std::string>>();
      consumer = [&digests, volumes](EventHits&& event) {
        digests << digest_to_json(make_digest(event), volumes).dump() << "\n";
        if (!digests) {
          throw std::runtime_error("writing the event digests failed");
        }
      };
    }
    if (out_path.empty()) {
      print_report(run_dev_simulation(spec, threads, consumer));
    }
    else {
      // The whole pipeline: hits.arrow, OID, provenance.json, run_summary.json, atomic folder.
      PipelineOptions options;
      options.threads = threads;
      options.extra_consumer = consumer;
      const PipelineResult result = run_to_folder(load.spec, out_path, options);
      print_report(result.table);
      std::cout << "\nOutput folder written\n";
      std::cout << "  oid                  " << result.oid << "\n";
      std::cout << "  hits                 " << result.hits_file.rows << " in "
                << result.hits_file.record_batches << " record batches, " << result.hits_file.bytes
                << " bytes\n";
      std::cout << "  events with hits     " << result.events_with_hits << "\n";
      std::cout << "  events without hits  " << result.events_without_hits << "\n";
    }
    if (!digests_path.empty()) {
      digests.close();
    }
    if (!digests_path.empty() && digests.fail()) {
      std::cerr << "writing " << digests_path << " failed\n";
      return kExitFailure;
    }
  }
  catch (const OutputError& e) {
    // OUTPUT_EXISTS is a usage error (exit 2), the others are failures (exit 1).
    std::cout << make_error_line(e.code(), e.what()).dump() << "\n";
    std::cerr << "dev-run failed: " << e.what() << "\n";
    return exit_code_for(e.code());
  }
  catch (const std::exception& e) {
    std::cerr << "dev-run failed: " << e.what() << "\n";
    return kExitFailure;
  }
  return kExitOk;
}

}  // namespace

void register_dev_run(CLI::App& app, int& exit_code)
{
  CLI::App* sub = app.add_subcommand(
      "dev-run", "EXPERIMENTAL: run a spec and print mean energy deposits (will be replaced by 'run')");
  auto path = std::make_shared<std::string>();
  auto threads = std::make_shared<int>(1);
  auto digests = std::make_shared<std::string>();
  auto out = std::make_shared<std::string>();
  sub->add_option("--spec", *path, "Path to the JSON spec")->required();
  sub->add_option("--threads", *threads, "Worker threads (default 1)")->check(CLI::PositiveNumber);
  sub->add_option("--out", *out,
                  "EXPERIMENTAL: write hits.arrow, provenance.json and run_summary.json to this new "
                  "folder (must not exist; written atomically)");
  sub->add_option("--event-digests", *digests,
                  "EXPERIMENTAL: write one JSON line per event, in event order, to this file");
  sub->footer(
      "Experimental and outside the sidecar contract. Prints a table on stdout; Geant4 output\n"
      "goes to stderr. Exit codes: 0 ok, 1 failure, 2 invalid spec or --out exists, 3 Geant4 data sets missing.\n"
      "\n"
      "--event-digests lines have the keys edep_by_volume_MeV, edep_sum_MeV, event_id, hash, hits,\n"
      "hits_by_volume. The hits are those in the volumes of the spec's 'sensitive' field. hash is\n"
      "FNV-1a 64 bit over the rows in step order, with positions and times quantised to 1e-6 mm\n"
      "and 1e-6 ns, energy to 1e-9 MeV and momentum to 1e-6 MeV/c (details: src/event_digest.hh).\n"
      "Events with no hits get a line with hits 0.");
  sub->callback([path, threads, digests, out, &exit_code]() {
    exit_code = run_dev(*path, *threads, *digests, *out);
  });
}
