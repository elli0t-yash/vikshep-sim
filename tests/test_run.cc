// `vikshep-sim run`: the sidecar contract end to end (POSIX only). Each run is a separate process.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "byte_compare.hh"
#include "cli_helpers.hh"
#include "datasets_check.hh"
#include "errors.hh"
#include "failure.hh"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

#define SKIP_WITHOUT_DATA_SETS()                                                         \
  do {                                                                                   \
    const auto missing = find_missing_data_sets(required_data_sets("FTFP_BERT"));        \
    if (!missing.empty()) {                                                              \
      SKIP("SKIPPED, not passed: Geant4 data sets are missing. " << describe_missing(missing)); \
    }                                                                                    \
  } while (0)

std::string read_file(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

json read_json(const fs::path& path)
{
  return json::parse(read_file(path));
}

json small_spec(std::uint64_t seed = 42)
{
  return {{"spec_version", "1.0"},
          {"template", "layered_calorimeter"},
          {"geometry",
           {{"layers", 3},
            {"absorber", {{"material", "G4_Pb"}, {"thickness_mm", 10}}},
            {"active", {{"material", "G4_lAr"}, {"thickness_mm", 5}}},
            {"transverse_mm", 100}}},
          {"gun", {{"particle", "e-"}, {"energy_GeV", 0.5}}},
          {"sensitive", json::array({"absorber", "active"})},
          {"events", 12},
          {"seed", seed}};
}

// A shell command with stdout captured; `prefix` can be "env -u G4LEDATA".
cli::Outcome shell(const std::string& command)
{
  cli::Outcome result;
  FILE* pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[4096];
  std::size_t n = 0;
  while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0) {
    result.out.append(buffer, n);
  }
  const int status = pclose(pipe);
  result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

cli::Outcome run_run(const std::string& spec_path, const std::string& out, const std::string& more = "",
                     const std::string& prefix = "")
{
  return shell(prefix + " \"" + VIKSHEP_SIM_EXE + "\" run --spec \"" + spec_path + "\" --out \"" + out + "\" " +
               more + " 2>/dev/null");
}

std::vector<json> lines_of(const std::string& text)
{
  std::vector<json> lines;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(json::parse(line));  // throws, failing the test, if a line is not JSON
  }
  return lines;
}

std::vector<std::string> entries(const std::string& dir)
{
  std::vector<std::string> names;
  for (const auto& e : fs::directory_iterator(dir)) {
    names.push_back(e.path().filename().string());
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace

TEST_CASE("a and c: run writes the same folder as dev-run --out", "[run][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec(small_spec().dump());
  cli::TempDir dir;
  const cli::Outcome a = run_run(spec.path(), dir.out("run"), "--threads 2 --progress jsonl");
  REQUIRE(a.code == 0);
  const cli::Outcome b =
      cli::run("dev-run --threads 2 --spec \"" + spec.path() + "\" --out \"" + dir.out("dev") + "\"");
  REQUIRE(b.code == 0);
  CHECK_SAME_BYTES(read_file(dir.out("run") + "/hits.arrow"), read_file(dir.out("dev") + "/hits.arrow"));
  CHECK_SAME_BYTES(read_file(dir.out("run") + "/provenance.json"), read_file(dir.out("dev") + "/provenance.json"));
  CHECK(read_json(dir.out("run") + "/run_summary.json").at("oid") ==
        read_json(dir.out("dev") + "/run_summary.json").at("oid"));

  // a: every stdout line of run is JSON with a known event, the last is done with that oid
  const auto lines = lines_of(a.out);
  REQUIRE(lines.size() >= 3);
  for (const json& l : lines) {
    const std::string event = l.at("event");
    CHECK((event == "progress" || event == "done" || event == "error"));
  }
  CHECK(lines.back().at("event") == "done");
  CHECK(lines.back().at("oid") == read_json(dir.out("run") + "/run_summary.json").at("oid"));
}

TEST_CASE("a: with --progress none stdout is empty, in success and in failure", "[run][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec(small_spec().dump());
  cli::TempDir dir;
  const cli::Outcome ok = run_run(spec.path(), dir.out("run"), "--threads 2");
  CHECK(ok.code == 0);
  CHECK(ok.out.empty());
  const cli::Outcome again = run_run(spec.path(), dir.out("run"), "--progress none");  // exists now
  CHECK(again.code == 2);
  CHECK(again.out.empty());
  const cli::TempFile bad(R"({"spec_version":"1.0"})");
  const cli::Outcome invalid = run_run(bad.path(), dir.out("bad"));
  CHECK(invalid.code == 2);
  CHECK(invalid.out.empty());
}

TEST_CASE("f: 1 thread and 4 threads give identical output through run", "[run][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec(small_spec(7).dump());
  cli::TempDir dir;
  const cli::Outcome one = run_run(spec.path(), dir.out("one"), "--threads 1 --progress jsonl");
  const cli::Outcome four = run_run(spec.path(), dir.out("four"), "--threads 4 --progress jsonl");
  REQUIRE(one.code == 0);
  REQUIRE(four.code == 0);
  CHECK_SAME_BYTES(read_file(dir.out("one") + "/hits.arrow"), read_file(dir.out("four") + "/hits.arrow"));
  CHECK_SAME_BYTES(read_file(dir.out("one") + "/provenance.json"), read_file(dir.out("four") + "/provenance.json"));
  CHECK(lines_of(one.out).back().at("oid") == lines_of(four.out).back().at("oid"));
  CHECK(read_json(dir.out("one") + "/provenance.json").at("build_type") == "Release");  // the default
}

TEST_CASE("d: an invalid spec gives exit 2, one SPEC_INVALID line and no folder", "[run]")
{
  const cli::TempFile spec(R"({"spec_version":"1.0","template":"layered_calorimeter",
    "geometry":{"layers":3,"absorber":{"material":"G4_Pb"},
                "active":{"material":"G4_lAr","thickness_mm":5},"transverse_mm":100},
    "gun":{"particle":"e-","energy_GeV":5},"events":0,"seed":42})");
  cli::TempDir dir;
  const cli::Outcome r = run_run(spec.path(), dir.out(), "--progress jsonl");
  CHECK(r.code == 2);
  const auto lines = lines_of(r.out);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].at("event") == "error");
  CHECK(lines[0].at("code") == "SPEC_INVALID");
  REQUIRE(lines[0].at("errors").size() == 2);
  CHECK(lines[0].at("errors")[0].at("pointer") == "/geometry/absorber");
  CHECK(lines[0].at("errors")[0].at("missing") == "thickness_mm");
  CHECK(lines[0].at("errors")[1].at("pointer") == "/events");
  CHECK_FALSE(lines[0].at("errors")[1].contains("missing"));
  CHECK(entries(dir.path()).empty());  // no folder, no temporary folder
}

TEST_CASE("d: unreadable and non-JSON spec files", "[run]")
{
  cli::TempDir dir;
  cli::Outcome r = run_run("/nonexistent/spec.json", dir.out(), "--progress jsonl");
  CHECK(r.code == 2);
  CHECK(lines_of(r.out).at(0).at("code") == "SPEC_UNREADABLE");
  const cli::TempFile junk("{ not json");
  r = run_run(junk.path(), dir.out(), "--progress jsonl");
  CHECK(r.code == 2);
  CHECK(lines_of(r.out).at(0).at("code") == "SPEC_NOT_JSON");
  CHECK(entries(dir.path()).empty());
}

TEST_CASE("d: an existing --out gives OUTPUT_EXISTS, exit 2, and is left untouched", "[run]")
{
  const cli::TempFile spec(small_spec().dump());
  cli::TempDir dir;
  fs::create_directories(dir.out("taken"));
  std::ofstream(fs::path(dir.out("taken")) / "marker.txt") << "keep";
  const cli::Outcome r = run_run(spec.path(), dir.out("taken"), "--progress jsonl");
  CHECK(r.code == 2);
  const auto lines = lines_of(r.out);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].at("code") == "OUTPUT_EXISTS");
  CHECK(entries(dir.out("taken")) == std::vector<std::string>{"marker.txt"});
  CHECK(read_file(fs::path(dir.out("taken")) / "marker.txt") == "keep");
  CHECK(entries(dir.path()) == std::vector<std::string>{"taken"});

  // a missing parent is the same code
  const cli::Outcome orphan = run_run(spec.path(), dir.out("nowhere/run"), "--progress jsonl");
  CHECK(orphan.code == 2);
  CHECK(lines_of(orphan.out).at(0).at("code") == "OUTPUT_EXISTS");
}

TEST_CASE("d: missing data sets give DATASETS_MISSING, exit 3 and no folder", "[run]")
{
  // The check happens before the simulation, so this needs no data sets itself.
  const cli::TempFile spec(small_spec().dump());
  cli::TempDir dir;
  const cli::Outcome r = run_run(spec.path(), dir.out(), "--progress jsonl", "env -u G4LEDATA");
  CHECK(r.code == 3);
  const auto lines = lines_of(r.out);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0].at("event") == "error");
  CHECK(lines[0].at("code") == "DATASETS_MISSING");
  CHECK(lines[0].at("message").get<std::string>().find("G4LEDATA") != std::string::npos);
  CHECK(entries(dir.path()).empty());
  // with --progress none: same exit code, stdout empty
  const cli::Outcome quiet = run_run(spec.path(), dir.out(), "", "env -u G4PARTICLEXSDATA");
  CHECK(quiet.code == 3);
  CHECK(quiet.out.empty());
}

TEST_CASE("d: an output failure is OUTPUT_NOT_WRITABLE, exit 1, and leaves nothing behind", "[run][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  // The file size limit makes the first full record batch fail to write, in the middle of the run.
  json spec = small_spec(3);
  spec["geometry"]["layers"] = 10;
  spec["gun"]["energy_GeV"] = 5;
  spec["events"] = 20;
  const cli::TempFile spec_file(spec.dump());
  cli::TempDir dir;
  const cli::TempFile script(std::string("ulimit -f 400\ntrap '' XFSZ\nexec \"") + VIKSHEP_SIM_EXE + "\" run --spec \"" +
                                 spec_file.path() + "\" --out \"" + dir.out() + "\" --threads 3 --progress jsonl\n",
                             ".sh");
  const cli::Outcome r = shell("/bin/sh \"" + script.path() + "\" 2>/dev/null");
  CHECK(r.code == 1);
  const auto lines = lines_of(r.out);
  REQUIRE_FALSE(lines.empty());
  CHECK(lines.back().at("event") == "error");
  CHECK(lines.back().at("code") == "OUTPUT_NOT_WRITABLE");
  CHECK(entries(dir.path()).empty());
}

TEST_CASE("exceptions map to stable codes; anything unexpected is INTERNAL", "[run]")
{
  auto classify = [](auto&& thrower) {
    try {
      thrower();
    }
    catch (...) {
      return classify_current_exception();
    }
    return Failure{};
  };
  CHECK(classify([] { throw CodedError("CANCELLED", "x"); }).code == "CANCELLED");
  CHECK(classify([] { throw CodedError("DATASETS_MISSING", "x"); }).code == "DATASETS_MISSING");
  CHECK(classify([] { throw WriteError("x"); }).code == "OUTPUT_NOT_WRITABLE");
  CHECK(classify([] { throw GeometryError("x"); }).code == "GEOMETRY_ERROR");
  CHECK(classify([] { throw IntegrityError("x"); }).code == "INTERNAL");
  CHECK(classify([] { throw std::runtime_error("x"); }).code == "SIMULATION_FAILED");
  CHECK(classify([] { throw std::logic_error("x"); }).code == "INTERNAL");
  CHECK(classify([] { throw std::bad_alloc(); }).code == "INTERNAL");
  CHECK(classify([] { throw 42; }).code == "INTERNAL");
  // the stdout message does not carry internals
  CHECK(classify([] { throw std::logic_error("secret /path/detail"); }).message == "internal error");
  CHECK(classify([] { throw std::logic_error("secret /path/detail"); }).detail.find("secret") != std::string::npos);
}

TEST_CASE("exit codes of the stable error codes", "[run]")
{
  for (const char* code : {"USAGE", "SPEC_INVALID", "SPEC_UNREADABLE", "SPEC_NOT_JSON", "SPEC_VERSION_UNSUPPORTED", "OUTPUT_EXISTS"}) {
    CHECK(exit_code_for(code) == 2);
  }
  CHECK(exit_code_for("DATASETS_MISSING") == 3);
  CHECK(exit_code_for("CANCELLED") == 4);
  for (const char* code : {"GEOMETRY_ERROR", "OUTPUT_NOT_WRITABLE", "SIMULATION_FAILED", "INTERNAL"}) {
    CHECK(exit_code_for(code) == 1);
  }
}

TEST_CASE("g: the reference consumer works against a real run", "[run][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  const cli::TempFile spec(small_spec().dump());
  cli::TempDir dir;
  const std::string consumer = std::string("\"") + VIKSHEP_PYTHON + "\" \"" + VIKSHEP_EXAMPLES_DIR + "/consume_run.py\" --exe \"" +
                               VIKSHEP_SIM_EXE + "\" --quiet";
  const cli::Outcome ok = shell(consumer + " --spec \"" + spec.path() + "\" --out \"" + dir.out() + "\" --threads 2 2>&1");
  INFO(ok.out);
  CHECK(ok.code == 0);
  CHECK(ok.out.find("events") != std::string::npos);       // the progress bar
  CHECK(ok.out.find("done. OID ") != std::string::npos);
  const std::string oid = read_json(fs::path(dir.out()) / "run_summary.json").at("oid");
  CHECK(ok.out.find(oid) != std::string::npos);
  CHECK(ok.out.find("hits.arrow") != std::string::npos);    // the folder listing
  CHECK(ok.out.find("provenance.json") != std::string::npos);

  // failure: the folder exists now; the consumer reports the code and returns exit code 2
  const cli::Outcome again = shell(consumer + " --spec \"" + spec.path() + "\" --out \"" + dir.out() + "\" 2>&1");
  CHECK(again.code == 2);
  CHECK(again.out.find("OUTPUT_EXISTS") != std::string::npos);

  // an invalid spec: the consumer shows the pointer and the missing field
  const cli::TempFile bad(R"({"spec_version":"1.0","template":"layered_calorimeter",
    "geometry":{"layers":3,"absorber":{"material":"G4_Pb"},"active":{"material":"G4_lAr","thickness_mm":5},
    "transverse_mm":100},"gun":{"particle":"e-","energy_GeV":5},"events":1,"seed":1})");
  const cli::Outcome invalid = shell(consumer + " --spec \"" + bad.path() + "\" --out \"" + dir.out("other") + "\" 2>&1");
  CHECK(invalid.code == 2);
  CHECK(invalid.out.find("SPEC_INVALID") != std::string::npos);
  CHECK(invalid.out.find("/geometry/absorber/thickness_mm") != std::string::npos);
}
