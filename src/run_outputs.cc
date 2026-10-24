#include "run_outputs.hh"

extern "C" {
#include "nanoarrow/nanoarrow.h"
}

#include "build_config.hh"
#include "build_info.hh"
#include "sha3_256.hh"
#include "spec_canonical.hh"
#include "versions.hh"

using nlohmann::json;

json make_provenance(const json& resolved_spec, const ProvenanceFacts& facts)
{
  json data_sets = json::array();
  for (const DataSetRecord& d : facts.data_sets) {
    data_sets.push_back({
        {"conda_build", d.conda_build},
        {"conda_package", d.conda_package},
        {"conda_version", d.conda_version},
        {"directory", d.directory},
        {"env_var", d.env_var},
    });
  }

  // The OID of the canonical form of the spec (docs/SPEC.md, section 4): the same
  // experiment gives the same spec_oid whatever the order of keys in the file.
  const std::string canonical = canonical_spec(resolved_spec);

  json j;
  j["contract_version"] = kContractVersion;
  j["data_sets"] = data_sets;
  j["default_production_cut_mm"] = facts.default_cut_mm;
  j["geant4"] = geant4_json();
  j["hits_schema_version"] = kHitsSchemaVersion;
  j["nanoarrow"] = {{"commit", kNanoarrowCommit}, {"version", NANOARROW_VERSION}};
  j["physics_list"] = resolved_spec.at("physics").at("list");
  j["platform"] = platform_json();
  j["run_manager"] = facts.run_manager;
  j["seed"] = resolved_spec.at("seed");
  j["seeding_scheme"] = kSeedingScheme;
  j["spec"] = resolved_spec;
  j["spec_oid"] = oid_of_bytes(canonical);
  j["spec_version"] = resolved_spec.at("spec_version");
  j["tool_version"] = VIKSHEP_SIM_VERSION;
  j["writer_scheme"] = kWriterScheme;
  return j;
}

json make_run_summary(const RunSummaryFacts& f)
{
  return {
      {"contract_version", kContractVersion},
      {"events_completed", f.events_completed},
      {"events_requested", f.events_requested},
      {"events_with_hits", f.events_with_hits},
      {"events_without_hits", f.events_without_hits},
      {"hits_file_bytes", f.hits_file_bytes},
      {"hits_schema_version", kHitsSchemaVersion},
      {"hits_total", f.hits_total},
      {"oid", f.oid},
      {"spec_version", kSpecVersion},
      {"threads", f.threads},
      {"wall_time_s", f.wall_time_s},
  };
}

std::string to_file_text(const json& document)
{
  return document.dump(2) + "\n";
}
