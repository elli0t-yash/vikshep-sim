// Unit tests of the 3-D export that need no simulation (docs/EXPORT3D.md): number formatting, the
// selection of tracks, the JSON layout, and the structure of geometry.glb.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cli_helpers.hh"
#include "errors.hh"
#include "export3d.hh"
#include "sha3_256.hh"
#include "spec_canonical.hh"

using nlohmann::json;

namespace {

TrackRecord track(int id, int parent, double energy_gev, int npoints = 2)
{
  TrackRecord t;
  t.track_id = id;
  t.parent_id = parent;
  t.pdg = parent == 0 ? 13 : 11;
  t.charge = parent == 0 ? -1 : -1;
  t.kinetic_energy_start_gev = energy_gev;
  for (int i = 0; i < npoints; ++i) {
    t.points.push_back({1.0 * i, 0.5 * i, 0.0});
  }
  return t;
}

json calorimeter_spec()
{
  return resolve_spec(json::parse(R"({
    "spec_version":"1.0","template":"layered_calorimeter",
    "geometry":{"layers":3,"absorber":{"material":"G4_Pb","thickness_mm":10},
                "active":{"material":"G4_lAr","thickness_mm":5},"transverse_mm":100},
    "gun":{"particle":"e-","energy_GeV":1},"events":2,"seed":1})"));
}

json tracker_spec()
{
  return resolve_spec(json::parse(R"({
    "spec_version":"1.1","template":"tracker_barrel",
    "geometry":{"layers":[{"radius_mm":50,"thickness_mm":0.5,"half_length_mm":300},
                          {"radius_mm":100,"thickness_mm":0.5,"half_length_mm":400}]},
    "gun":{"particle":"mu-","energy_GeV":1},"events":2,"seed":1})"));
}

std::uint32_t u32(const std::string& s, std::size_t at)
{
  std::uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(s[at + i]);
  return v;
}

// The JSON of a .glb, after checking the container (EXPORT3D.md 2.1).
json parse_glb(const std::string& glb, std::string& bin)
{
  REQUIRE(glb.size() > 28);
  CHECK(u32(glb, 0) == 0x46546C67);
  CHECK(u32(glb, 4) == 2);
  CHECK(u32(glb, 8) == glb.size());
  const std::uint32_t json_length = u32(glb, 12);
  CHECK(u32(glb, 16) == 0x4E4F534A);
  CHECK(json_length % 4 == 0);
  const std::string text = glb.substr(20, json_length);
  const std::size_t bin_at = 20 + json_length;
  const std::uint32_t bin_length = u32(glb, bin_at);
  CHECK(u32(glb, bin_at + 4) == 0x004E4942);
  CHECK(bin_length % 4 == 0);
  CHECK(bin_at + 8 + bin_length == glb.size());
  bin = glb.substr(bin_at + 8, bin_length);
  return json::parse(text);
}

}  // namespace

TEST_CASE("fixed-precision formatting", "[export3d]")
{
  CHECK(format_fixed(1.0, 3) == "1.000");
  CHECK(format_fixed(-0.0, 3) == "0.000");
  CHECK(format_fixed(-0.0004, 3) == "0.000");
  CHECK(format_fixed(-0.0006, 3) == "-0.001");
  CHECK(format_fixed(1234567.8915, 3).substr(0, 10) == "1234567.89");
  CHECK(format_fixed(0.001, 6) == "0.001000");
  CHECK(format_fixed(1e-9, 6) == "0.000000");
  CHECK(format_fixed(1e20, 3).find('e') == std::string::npos);
  CHECK_THROWS_AS(format_fixed(std::nan(""), 3), ExportError);
  CHECK_THROWS_AS(format_fixed(std::numeric_limits<double>::infinity(), 3), ExportError);
}

TEST_CASE("the header of trajectories.json", "[export3d]")
{
  ExportOptions o;
  o.enabled = true;
  o.events = 7;
  o.min_energy_gev = 0.0025;
  CHECK(trajectories_header("0123456789abcdef0123456789ab", o) ==
        "{\"format_version\":\"1.0\",\"unit\":\"mm\",\"spec_oid\":\"0123456789abcdef0123456789ab\","
        "\"min_energy_GeV\":0.002500,\"max_tracks_per_event\":500,\"events_requested\":7,\"events\":[\n");
}

TEST_CASE("one event of trajectories.json, byte for byte", "[export3d]")
{
  EventTracks e;
  e.tracks_total = 5;
  TrackRecord t = track(1, 0, 1.0, 0);
  t.points = {{0, 0, 0}, {49.8454, -0.0004, 1.0005}};
  e.tracks.push_back(t);
  e.tracks.push_back(track(3, 1, 0.0123456789, 1));
  CHECK(e.dropped() == 3);
  CHECK(trajectories_event_json(4, e) ==
        "{\"event_id\":4,\"tracks_total\":5,\"dropped_tracks\":3,\"tracks\":["
        "{\"track_id\":1,\"parent_id\":0,\"pdg\":13,\"charge\":-1,\"kinetic_energy_start_GeV\":1.000000,"
        "\"points\":[[0.000,0.000,0.000],[49.845,0.000,1.000]]},"
        "{\"track_id\":3,\"parent_id\":1,\"pdg\":11,\"charge\":-1,\"kinetic_energy_start_GeV\":0.012346,"
        "\"points\":[[0.000,0.000,0.000]]}]}");
}

TEST_CASE("under the cap every eligible track is kept, in track_id order", "[export3d]")
{
  const EventTracks r = select_tracks({track(5, 1, 0.5), track(1, 0, 1.0), track(3, 1, 0.2)}, 40);
  REQUIRE(r.tracks.size() == 3);
  CHECK(r.tracks[0].track_id == 1);
  CHECK(r.tracks[1].track_id == 3);
  CHECK(r.tracks[2].track_id == 5);
  CHECK(r.tracks_total == 40);
  CHECK(r.dropped() == 37);
}

TEST_CASE("over the cap: primaries first, then the most energetic, ties by track_id", "[export3d]")
{
  std::vector<TrackRecord> many;
  many.push_back(track(900, 0, 0.0001));  // a primary of tiny energy is kept anyway
  for (int i = 1; i <= 700; ++i) {
    many.push_back(track(i, 1, 0.001 + 1e-6 * (i % 50)));  // many ties in energy
  }
  const EventTracks r = select_tracks(many, 701);
  REQUIRE(r.tracks.size() == 500);
  CHECK(r.dropped() == 201);
  bool has_primary = false;
  double lowest = 1e9;
  for (std::size_t i = 0; i < r.tracks.size(); ++i) {
    if (i > 0) CHECK(r.tracks[i - 1].track_id < r.tracks[i].track_id);
    if (r.tracks[i].parent_id == 0) has_primary = true;
    else lowest = std::min(lowest, r.tracks[i].kinetic_energy_start_gev);
  }
  CHECK(has_primary);
  // every kept secondary is at least as energetic as every dropped one
  for (const TrackRecord& t : many) {
    bool kept = false;
    for (const TrackRecord& k : r.tracks) kept = kept || k.track_id == t.track_id;
    if (!kept) CHECK(t.kinetic_energy_start_gev <= lowest);
  }
  // the choice does not depend on the order the tracks arrive in
  std::vector<TrackRecord> reversed(many.rbegin(), many.rend());
  const EventTracks r2 = select_tracks(reversed, 701);
  REQUIRE(r2.tracks.size() == r.tracks.size());
  for (std::size_t i = 0; i < r.tracks.size(); ++i) CHECK(r.tracks[i].track_id == r2.tracks[i].track_id);
}

TEST_CASE("the trajectory writer writes the documented layout and reports size and OID", "[export3d]")
{
  const cli::TempFile file("", ".json");
  ExportOptions o;
  o.enabled = true;
  o.events = 2;
  TrajectoryWriter w(file.path(), "0123456789abcdef0123456789ab", o);
  EventTracks a;
  a.tracks_total = 1;
  a.tracks.push_back(track(1, 0, 1.0));
  w.add_event(0, a);
  w.add_event(1, a);
  const ExportedFile f = w.finish();
  std::ifstream in(file.path(), std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(f.bytes == text.size());
  CHECK(f.oid == oid_of_bytes(text));
  CHECK(f.oid.size() == 28);
  CHECK(text.back() == '\n');
  CHECK(text.substr(text.size() - 4) == "\n]}\n");
  const json j = json::parse(text);
  CHECK(j.at("events").size() == 2);
  CHECK(j.at("events")[1].at("event_id") == 1);
  // the header is one line, then one line per event
  std::size_t lines = 0;
  for (char c : text) lines += c == '\n';
  CHECK(lines == 1 + 2 + 1);  // header, two events, closing
}

TEST_CASE("geometry.glb of the calorimeter", "[export3d][glb]")
{
  const json spec = calorimeter_spec();
  const std::string glb = build_geometry_glb(spec);
  CHECK(glb == build_geometry_glb(spec));  // deterministic
  std::string bin;
  const json g = parse_glb(glb, bin);
  CHECK(g.at("asset").at("version") == "2.0");
  const json& extras = g.at("asset").at("extras");
  CHECK(extras.at("length_unit") == "m");
  CHECK(extras.at("mm_per_unit") == 1000);
  CHECK(extras.at("beam_axis") == "+z");
  CHECK(extras.at("frame") == "geant4_global");
  CHECK(extras.at("template") == "layered_calorimeter");
  CHECK(extras.at("spec_oid") == spec_oid_of(spec));
  CHECK_FALSE(extras.contains("tube_segments"));

  const json& nodes = g.at("nodes");
  REQUIRE(nodes.size() == 6);  // 2 * layers
  CHECK(g.at("meshes").size() == 2);
  CHECK(g.at("scenes")[0].at("nodes").size() == 6);
  for (int i = 0; i < 6; ++i) {
    const json& n = nodes[i];
    CHECK(n.at("name") == (i % 2 == 0 ? "absorber" : "active"));
    CHECK(n.at("extras").at("copy_no") == i / 2);
    CHECK(n.at("extras").at("role") == (i % 2 == 0 ? "absorber" : "active"));
    CHECK(n.at("extras").at("sensitive") == (i % 2 == 1));  // the default sensitive list is ["active"]
  }
  // 3 layers of 10 + 5 mm, centred: the first absorber is at z = -22.5 + 5 = -17.5 mm
  CHECK(nodes[0].at("translation")[2].get<double>() == Catch::Approx(-0.0175).epsilon(1e-6));
  CHECK(nodes[1].at("translation")[2].get<double>() == Catch::Approx(-0.0100).epsilon(1e-6));
  // box meshes: 24 vertices, 36 indices
  for (int m = 0; m < 2; ++m) {
    CHECK(g.at("accessors")[3 * m].at("count") == 24);
    CHECK(g.at("accessors")[3 * m + 2].at("count") == 36);
  }
  CHECK(g.at("buffers")[0].at("byteLength") == bin.size());
  // the absorber box is 100 mm square and 10 mm thick, in metres
  const json& max = g.at("accessors")[0].at("max");
  CHECK(max[0].get<double>() == Catch::Approx(0.05).epsilon(1e-6));
  CHECK(max[2].get<double>() == Catch::Approx(0.005).epsilon(1e-6));
}

TEST_CASE("geometry.glb of the tracker", "[export3d][glb]")
{
  const json spec = tracker_spec();
  const std::string glb = build_geometry_glb(spec);
  CHECK(glb == build_geometry_glb(spec));
  std::string bin;
  const json g = parse_glb(glb, bin);
  CHECK(g.at("asset").at("extras").at("tube_segments") == 64);
  CHECK(g.at("asset").at("extras").at("template") == "tracker_barrel");
  const json& nodes = g.at("nodes");
  REQUIRE(nodes.size() == 2);  // N layers
  CHECK(g.at("meshes").size() == 2);
  for (int i = 0; i < 2; ++i) {
    CHECK(nodes[i].at("name") == "tracker_layer");
    CHECK(nodes[i].at("extras").at("copy_no") == i);
    CHECK(nodes[i].at("extras").at("role") == "tracker");
    CHECK(nodes[i].at("extras").at("sensitive") == true);
    CHECK_FALSE(nodes[i].contains("translation"));
    CHECK(g.at("accessors")[3 * i].at("count") == 512);
    CHECK(g.at("accessors")[3 * i + 2].at("count") == 1536);
    CHECK(g.at("meshes")[i].at("name") == "tracker_layer_" + std::to_string(i));
  }
  // layer 1: outer radius 100.25 mm, half-length 400 mm
  const json& max = g.at("accessors")[3].at("max");
  CHECK(max[0].get<double>() == Catch::Approx(0.10025).epsilon(1e-6));
  CHECK(max[2].get<double>() == Catch::Approx(0.4).epsilon(1e-6));
}

TEST_CASE("geometry.glb follows the sensitive list", "[export3d][glb]")
{
  json spec = calorimeter_spec();
  spec["sensitive"] = json::array({"absorber", "active"});
  std::string bin;
  const json g = parse_glb(build_geometry_glb(spec), bin);
  for (const json& n : g.at("nodes")) CHECK(n.at("extras").at("sensitive") == true);
  spec["sensitive"] = json::array({"absorber"});
  const json h = parse_glb(build_geometry_glb(spec), bin);
  CHECK(h.at("nodes")[0].at("extras").at("sensitive") == true);
  CHECK(h.at("nodes")[1].at("extras").at("sensitive") == false);
}

TEST_CASE("write_geometry_glb reports the size and OID of the file", "[export3d][glb]")
{
  const cli::TempFile file("", ".glb");
  const json spec = tracker_spec();
  const ExportedFile f = write_geometry_glb(file.path(), spec);
  const std::string bytes = build_geometry_glb(spec);
  CHECK(f.bytes == bytes.size());
  CHECK(f.oid == oid_of_bytes(bytes));
  CHECK_THROWS_AS(write_geometry_glb("/nonexistent-directory/geometry.glb", spec), ExportError);
}
