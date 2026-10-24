// scripts/plot_event.py and scripts/compare_particles.sh (POSIX only).

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cli_helpers.hh"
#include "data_sets_guard.hh"
#include "datasets_check.hh"

using nlohmann::json;
namespace fs = std::filesystem;

namespace {


json small_spec(const json& direction = nullptr)
{
  json s = {{"spec_version", "1.0"},
            {"template", "layered_calorimeter"},
            {"geometry",
             {{"layers", 3},
              {"absorber", {{"material", "G4_Pb"}, {"thickness_mm", 10}}},
              {"active", {{"material", "G4_lAr"}, {"thickness_mm", 5}}},
              {"transverse_mm", 100}}},
            {"gun", {{"particle", "e-"}, {"energy_GeV", 0.5}}},
            {"sensitive", json::array({"absorber", "active"})},
            {"events", 6},
            {"seed", 11}};
  if (!direction.is_null()) {
    s["gun"]["direction"] = direction;
  }
  return s;
}

cli::Outcome run_command(const std::string& command)
{
  cli::Outcome result;
  FILE* pipe = popen((command + " 2>&1").c_str(), "r");
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

cli::Outcome plot(const std::string& args)
{
  return run_command(std::string("\"") + VIKSHEP_PYTHON + "\" \"" + VIKSHEP_SCRIPTS_DIR + "/plot_event.py\" " + args);
}

// Makes a run folder with dev-run --out.
void make_run(const json& spec, const std::string& out)
{
  const cli::TempFile spec_file(spec.dump());
  REQUIRE(cli::run("dev-run --threads 2 --spec \"" + spec_file.path() + "\" --out \"" + out + "\"").code == 0);
}

// The width and height from the IHDR chunk of a PNG, or false if it is not a PNG.
bool png_size(const std::string& path, std::uint32_t& width, std::uint32_t& height)
{
  std::ifstream in(path, std::ios::binary);
  unsigned char header[24];
  in.read(reinterpret_cast<char*>(header), sizeof header);
  if (in.gcount() != 24) return false;
  const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  for (int i = 0; i < 8; ++i) {
    if (header[i] != signature[i]) return false;
  }
  auto be32 = [&](int at) {
    return (std::uint32_t(header[at]) << 24) | (std::uint32_t(header[at + 1]) << 16) |
           (std::uint32_t(header[at + 2]) << 8) | std::uint32_t(header[at + 3]);
  };
  width = be32(16);
  height = be32(20);
  return true;
}

}  // namespace

TEST_CASE("plot_event.py writes a non-empty PNG for one event", "[plot][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  make_run(small_spec(), dir.out("run"));
  const std::string png = dir.out("shower.png");
  const cli::Outcome r = plot("\"" + dir.out("run") + "\" --event 0 --out \"" + png + "\"");
  INFO(r.out);
  REQUIRE(r.code == 0);
  REQUIRE(fs::exists(png));
  CHECK(fs::file_size(png) > 10000);  // a real picture, not an empty canvas
  std::uint32_t w = 0, h = 0;
  REQUIRE(png_size(png, w, h));
  CHECK(w > 1000);
  CHECK(h > 400);
  CHECK(w > h);  // two panels side by side
}

TEST_CASE("plot_event.py draws the last event, and several runs side by side", "[plot][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  make_run(small_spec(), dir.out("a"));
  json pion = small_spec();
  pion["gun"]["particle"] = "pi-";
  make_run(pion, dir.out("b"));

  CHECK(plot("\"" + dir.out("a") + "\" --event 5 --out \"" + dir.out("last.png") + "\"").code == 0);
  CHECK(fs::file_size(dir.out("last.png")) > 10000);

  const cli::Outcome r = plot("\"" + dir.out("a") + "\" \"" + dir.out("b") + "\" --event 2 --out \"" +
                              dir.out("both.png") + "\"");
  INFO(r.out);
  REQUIRE(r.code == 0);
  std::uint32_t w = 0, h = 0, w1 = 0, h1 = 0;
  REQUIRE(png_size(dir.out("both.png"), w, h));
  REQUIRE(png_size(dir.out("last.png"), w1, h1));
  CHECK(h > h1);  // two rows (hits above, layers below): taller than the one-run figure
}

TEST_CASE("plot_event.py handles a run without any hit", "[plot][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  make_run(small_spec(json::array({1, 0, 0})), dir.out("run"));  // gun aimed out of the world
  const cli::Outcome r = plot("\"" + dir.out("run") + "\" --event 0 --out \"" + dir.out("empty.png") + "\"");
  INFO(r.out);
  CHECK(r.code == 0);
  CHECK(fs::file_size(dir.out("empty.png")) > 5000);
}

TEST_CASE("plot_event.py says what is wrong", "[plot][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  make_run(small_spec(), dir.out("run"));

  cli::Outcome r = plot("\"" + dir.out("run") + "\" --event 99 --out \"" + dir.out("x.png") + "\"");
  CHECK(r.code != 0);
  CHECK(r.out.find("event 99 does not exist") != std::string::npos);
  CHECK_FALSE(fs::exists(dir.out("x.png")));

  r = plot("\"" + dir.out("nonexistent") + "\" --event 0 --out \"" + dir.out("y.png") + "\"");
  CHECK(r.code != 0);
  CHECK(r.out.find("is not a run folder") != std::string::npos);
  CHECK_FALSE(fs::exists(dir.out("y.png")));
}

TEST_CASE("compare_particles.sh runs the electron and the pion and plots them", "[plot][geant4]")
{
  SKIP_WITHOUT_DATA_SETS();
  cli::TempDir dir;
  const std::string out = dir.out("compare");
  const cli::Outcome r = run_command(std::string("PYTHON=\"") + VIKSHEP_PYTHON + "\" VIKSHEP_SIM=\"" +
                                     VIKSHEP_SIM_EXE + "\" \"" + VIKSHEP_SCRIPTS_DIR +
                                     "/compare_particles.sh\" \"" + out + "\" 2");
  INFO(r.out);
  REQUIRE(r.code == 0);
  for (const char* name : {"electron.png", "pion.png", "compare.png"}) {
    INFO(name);
    CHECK(fs::exists(fs::path(out) / name));
    CHECK(fs::file_size(fs::path(out) / name) > 10000);
  }
  CHECK(fs::exists(fs::path(out) / "electron" / "hits.arrow"));
  CHECK(fs::exists(fs::path(out) / "pion" / "run_summary.json"));
  // The two runs are different experiments: different particles, different OIDs.
  const json e = json::parse(std::ifstream(fs::path(out) / "electron" / "run_summary.json"));
  const json p = json::parse(std::ifstream(fs::path(out) / "pion" / "run_summary.json"));
  CHECK(e.at("oid") != p.at("oid"));
  CHECK(json::parse(std::ifstream(fs::path(out) / "pion" / "provenance.json")).at("spec").at("gun").at("particle") == "pi-");

  // A second call with the same folder is refused (it never overwrites).
  const cli::Outcome again = run_command(std::string("PYTHON=\"") + VIKSHEP_PYTHON + "\" VIKSHEP_SIM=\"" +
                                         VIKSHEP_SIM_EXE + "\" \"" + VIKSHEP_SCRIPTS_DIR +
                                         "/compare_particles.sh\" \"" + out + "\" 2");
  CHECK(again.code == 2);
  CHECK(again.out.find("already exists") != std::string::npos);
}

TEST_CASE("compare_particles.sh refuses a missing executable", "[plot]")
{
  cli::TempDir dir;
  const cli::Outcome r = run_command(std::string("VIKSHEP_SIM=/nonexistent/vikshep-sim \"") + VIKSHEP_SCRIPTS_DIR +
                                     "/compare_particles.sh\" \"" + dir.out("c") + "\"");
  CHECK(r.code == 2);
  CHECK(r.out.find("not found") != std::string::npos);
  CHECK_FALSE(fs::exists(dir.out("c")));
}
