// Unit tests of spec 1.1: the tracker_barrel template, the field block, version handling and
// the canonical form (docs/SPEC.md, sections 2.5 to 2.7, 4). No simulation is run.

#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "sha3_256.hh"
#include "spec_canonical.hh"
#include "spec_catalogue.hh"
#include "spec_validate.hh"

using nlohmann::json;

namespace {

const Catalogue& catalogue()
{
  static const Catalogue c = load_geant4_catalogue();
  return c;
}

json read_example(const std::string& name)
{
  std::ifstream in(std::string(VIKSHEP_EXAMPLES_DIR) + "/" + name);
  REQUIRE(in.good());
  return json::parse(in);
}

json tracker()
{
  return read_example("tracker_muon.json");
}

json with(json spec, const std::string& pointer, const json& value)
{
  spec[json::json_pointer(pointer)] = value;
  return spec;
}

json without(json spec, const std::string& pointer)
{
  json::json_pointer p(pointer);
  spec[p.parent_pointer()].erase(p.back());
  return spec;
}

ValidationResult check(const json& spec)
{
  return validate_spec(spec, catalogue());
}

std::vector<std::string> pointers(const ValidationResult& r)
{
  std::vector<std::string> out;
  for (const SpecIssue& i : r.issues) {
    out.push_back(i.pointer);
  }
  return out;
}

const SpecIssue* issue_at(const ValidationResult& r, const std::string& pointer)
{
  for (const SpecIssue& i : r.issues) {
    if (i.pointer == pointer) {
      return &i;
    }
  }
  return nullptr;
}

bool says(const ValidationResult& r, const std::string& pointer, const std::string& text)
{
  const SpecIssue* i = issue_at(r, pointer);
  return i != nullptr && i->message.find(text) != std::string::npos;
}

// Two layers, small enough to write out the canonical form in full.
json small_tracker()
{
  return json::parse(R"({
    "spec_version": "1.1", "template": "tracker_barrel",
    "geometry": {"layers": [
      {"radius_mm": 50, "thickness_mm": 0.5, "half_length_mm": 300},
      {"radius_mm": 100, "thickness_mm": 0.5, "half_length_mm": 400, "material": "G4_Ge"}]},
    "gun": {"particle": "mu-", "energy_GeV": 1}, "events": 5, "seed": 1})");
}

}  // namespace

// ---------------------------------------------------------------- versions

TEST_CASE("the tracker example is valid", "[spec11]")
{
  const ValidationResult r = check(tracker());
  CHECK(r.ok());
  CHECK(r.issues.empty());
}

TEST_CASE("both spec versions are accepted, others are not", "[spec11]")
{
  CHECK(check(read_example("calorimeter.json")).ok());  // written as 1.0
  CHECK(check(with(read_example("calorimeter.json"), "/spec_version", "1.1")).ok());
  for (const char* version : {"1.2", "0.9", "2.0", "1", ""}) {
    DYNAMIC_SECTION("version " << version)
    {
      const ValidationResult r = check(with(read_example("calorimeter.json"), "/spec_version", version));
      CHECK(r.code == "SPEC_VERSION_UNSUPPORTED");
      REQUIRE(r.issues.size() == 1);
      CHECK(r.issues[0].pointer == "/spec_version");
      CHECK(r.issues[0].message.find("\"1.0\" and \"1.1\"") != std::string::npos);
    }
  }
}

TEST_CASE("1.1-only elements in a 1.0 spec are errors that name the version", "[spec11]")
{
  const ValidationResult t = check(with(tracker(), "/spec_version", "1.0"));
  CHECK_FALSE(t.ok());
  CHECK(says(t, "/template", "requires spec_version \"1.1\""));
  // the geometry is still checked as a tracker's, so the one mistake is reported once
  CHECK(pointers(t) == std::vector<std::string>{"/template", "/field"});

  const ValidationResult f = check(with(read_example("calorimeter.json"), "/field", json{{"B_T", {0, 0, 1}}}));
  CHECK(pointers(f) == std::vector<std::string>{"/field"});
  CHECK(says(f, "/field", "requires spec_version \"1.1\""));
}

TEST_CASE("the 1.0 template error text is unchanged", "[spec11]")
{
  const ValidationResult r = check(with(read_example("calorimeter.json"), "/template", "calo"));
  CHECK(says(r, "/template", "must be \"layered_calorimeter\", the only template in spec 1.0"));
  const ValidationResult r11 = check(with(with(read_example("calorimeter.json"), "/template", "calo"),
                                          "/spec_version", "1.1"));
  CHECK(says(r11, "/template", "\"layered_calorimeter\" or \"tracker_barrel\""));
}

// ---------------------------------------------------------------- tracker_barrel: shape

TEST_CASE("layers must be an array of 1 to 100 objects", "[spec11]")
{
  CHECK(pointers(check(with(tracker(), "/geometry/layers", 6))) == std::vector<std::string>{"/geometry/layers"});
  CHECK(pointers(check(with(tracker(), "/geometry/layers", json::array()))) ==
        std::vector<std::string>{"/geometry/layers"});
  json many = json::array();
  for (int i = 0; i < 101; ++i) {
    many.push_back({{"radius_mm", 10.0 + 10.0 * i}, {"thickness_mm", 1}, {"half_length_mm", 100}});
  }
  CHECK(pointers(check(with(tracker(), "/geometry/layers", many))) == std::vector<std::string>{"/geometry/layers"});
  many.erase(many.begin() + 100);
  CHECK(check(with(tracker(), "/geometry/layers", many)).ok());  // 100 layers: the limit
  CHECK(pointers(check(without(tracker(), "/geometry/layers"))) == std::vector<std::string>{"/geometry"});
  const ValidationResult r = check(with(tracker(), "/geometry/layers/1", "x"));
  CHECK(pointers(r) == std::vector<std::string>{"/geometry/layers/1"});
}

TEST_CASE("a missing layer field is reported against the element", "[spec11]")
{
  const ValidationResult r = check(without(tracker(), "/geometry/layers/2/radius_mm"));
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].pointer == "/geometry/layers/2");
  CHECK(r.issues[0].missing == "radius_mm");
  for (const char* key : {"thickness_mm", "half_length_mm"}) {
    const ValidationResult m = check(without(tracker(), std::string("/geometry/layers/4/") + key));
    REQUIRE(m.issues.size() == 1);
    CHECK(m.issues[0].pointer == "/geometry/layers/4");
    CHECK(m.issues[0].missing == key);
  }
  // material is optional
  CHECK(check(without(with(tracker(), "/geometry/layers/0/material", "G4_Si"), "/geometry/layers/0/material")).ok());
}

TEST_CASE("layer numbers have ranges, with the exact element in the pointer", "[spec11]")
{
  struct Case {
    const char* pointer;
    json value;
    const char* text;
  };
  const std::vector<Case> cases = {
      {"/geometry/layers/3/radius_mm", 0, "greater than 0"},
      {"/geometry/layers/3/radius_mm", -5, "greater than 0"},
      {"/geometry/layers/3/radius_mm", 10001, "at most 10000"},
      {"/geometry/layers/3/radius_mm", "200", "must be a number"},
      {"/geometry/layers/5/thickness_mm", 0, "greater than 0"},
      {"/geometry/layers/5/thickness_mm", 300, "less than radius_mm of the same layer (300 mm)"},
      {"/geometry/layers/5/thickness_mm", 301, "less than radius_mm"},
      {"/geometry/layers/1/half_length_mm", 0, "greater than 0"},
      {"/geometry/layers/1/half_length_mm", 10001, "at most 10000"},
      {"/geometry/layers/1/half_length_mm", true, "must be a number"},
  };
  for (const Case& c : cases) {
    DYNAMIC_SECTION(c.pointer << " = " << c.value.dump())
    {
      const ValidationResult r = check(with(tracker(), c.pointer, c.value));
      REQUIRE(r.issues.size() == 1);
      CHECK(r.issues[0].pointer == c.pointer);
      CHECK(r.issues[0].message.find(c.text) != std::string::npos);
    }
  }
  // the limits themselves are valid: the largest radius with a thin wall, the longest layer
  json edge = with(with(tracker(), "/geometry/layers/5/radius_mm", 10000), "/geometry/layers/5/half_length_mm", 10000);
  CHECK(check(edge).ok());
  // thickness just below the radius is valid for a single layer (but a thick wall at layer 0)
  json thick = with(tracker(), "/geometry/layers", json::array({{{"radius_mm", 10}, {"thickness_mm", 9.999},
                                                                 {"half_length_mm", 5}}}));
  CHECK(check(thick).ok());
}

TEST_CASE("layer materials and unknown layer fields", "[spec11]")
{
  const ValidationResult m = check(with(tracker(), "/geometry/layers/3/material", "G4_Sii"));
  REQUIRE(m.issues.size() == 1);
  CHECK(m.issues[0].pointer == "/geometry/layers/3/material");
  CHECK(m.issues[0].message.find("did you mean 'G4_Si'") != std::string::npos);
  CHECK(check(with(tracker(), "/geometry/layers/3/material", "G4_Ge")).ok());

  const ValidationResult u = check(with(tracker(), "/geometry/layers/1/radis_mm", 5));
  REQUIRE(u.issues.size() == 1);
  CHECK(u.issues[0].pointer == "/geometry/layers/1/radis_mm");
  CHECK(u.issues[0].message.find("did you mean 'radius_mm'") != std::string::npos);

  // the calorimeter's fields do not exist for the tracker
  const ValidationResult c = check(with(tracker(), "/geometry/transverse_mm", 100));
  REQUIRE(c.issues.size() == 1);
  CHECK(c.issues[0].pointer == "/geometry/transverse_mm");
  CHECK(c.issues[0].message.find("allowed fields: layers") != std::string::npos);
  // and the tracker's layers do not exist for the calorimeter
  const ValidationResult d = check(with(read_example("calorimeter.json"), "/geometry/layers", json::array()));
  CHECK(pointers(d) == std::vector<std::string>{"/geometry/layers"});
}

// ---------------------------------------------------------------- tracker_barrel: ordering

TEST_CASE("equal or decreasing radii are rejected at the later element", "[spec11]")
{
  // layer 2 at the radius of layer 1
  ValidationResult r = check(with(tracker(), "/geometry/layers/2/radius_mm", 100));
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].pointer == "/geometry/layers/2/radius_mm");
  CHECK(r.issues[0].message ==
        "radius_mm must be greater than the radius of layer 1 (100 mm); layers are numbered from the innermost "
        "and radii must strictly increase");
  // layer 3 inside layer 2
  r = check(with(tracker(), "/geometry/layers/3/radius_mm", 120));
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].pointer == "/geometry/layers/3/radius_mm");
  CHECK(says(r, "/geometry/layers/3/radius_mm", "layer 2 (150 mm)"));
  // layer 0 has no predecessor; making layer 1 smaller than layer 0 is reported at layer 1 only:
  // layer 2 (150 mm) is above layer 1 (40 mm) and is compared with that
  r = check(with(tracker(), "/geometry/layers/1/radius_mm", 40));
  CHECK(pointers(r) == std::vector<std::string>{"/geometry/layers/1/radius_mm"});
}

TEST_CASE("overlapping or touching layers are rejected at the later element", "[spec11]")
{
  // radii increase but the walls overlap: 100 +- 0.15 and 100.2 +- 0.15
  json s = with(tracker(), "/geometry/layers/1/radius_mm", 100.2);
  s["geometry"]["layers"][0]["radius_mm"] = 100;
  s["geometry"]["layers"] = json::array({s["geometry"]["layers"][0], s["geometry"]["layers"][1]});
  ValidationResult r = check(s);
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].pointer == "/geometry/layers/1/radius_mm");
  CHECK(r.issues[0].message.find("overlaps or touches layer 0") != std::string::npos);

  // exactly touching (all values exact in float64): outer of 0 = 101, inner of 1 = 101
  const json touching = json::array({{{"radius_mm", 100}, {"thickness_mm", 2}, {"half_length_mm", 50}},
                                     {{"radius_mm", 102}, {"thickness_mm", 2}, {"half_length_mm", 50}}});
  r = check(with(tracker(), "/geometry/layers", touching));
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].pointer == "/geometry/layers/1/radius_mm");
  CHECK(r.issues[0].message.find("overlaps or touches") != std::string::npos);

  // a gap is fine
  const json gap = json::array({{{"radius_mm", 100}, {"thickness_mm", 2}, {"half_length_mm", 50}},
                                {{"radius_mm", 102.5}, {"thickness_mm", 2}, {"half_length_mm", 50}}});
  CHECK(check(with(tracker(), "/geometry/layers", gap)).ok());

  // a thick layer swallowing the next: pointer at the swallowed element
  const json thick = json::array({{{"radius_mm", 100}, {"thickness_mm", 90}, {"half_length_mm", 50}},
                                  {{"radius_mm", 120}, {"thickness_mm", 1}, {"half_length_mm", 50}}});
  r = check(with(tracker(), "/geometry/layers", thick));
  CHECK(pointers(r) == std::vector<std::string>{"/geometry/layers/1/radius_mm"});
}

TEST_CASE("several ordering errors are all reported, in index order", "[spec11]")
{
  json s = with(tracker(), "/geometry/layers/1/radius_mm", 50);   // layer 1 not above layer 0
  s = with(s, "/geometry/layers/4/radius_mm", 150);               // layer 4 below layer 3
  const ValidationResult r = check(s);
  // layer 2 (150) is compared with layer 1 (50) and passes; layer 4 (150) is below layer 3 (200);
  // layer 5 (300) is compared with layer 4 (150) and passes
  CHECK(pointers(r) == std::vector<std::string>{"/geometry/layers/1/radius_mm", "/geometry/layers/4/radius_mm"});
}

TEST_CASE("an invalid layer is not compared with its neighbours", "[spec11]")
{
  // layer 2 has an invalid radius: only that is reported; layer 3 is compared with nothing
  const ValidationResult r = check(with(tracker(), "/geometry/layers/2/radius_mm", -1));
  CHECK(pointers(r) == std::vector<std::string>{"/geometry/layers/2/radius_mm"});
}

TEST_CASE("ordering errors do not hide the gun position check", "[spec11]")
{
  json s = with(tracker(), "/geometry/layers/2/radius_mm", 100);
  s["gun"]["position_mm"] = {0, 0, 5000};
  const ValidationResult r = check(s);
  CHECK(pointers(r) == std::vector<std::string>{"/geometry/layers/2/radius_mm", "/gun/position_mm"});
}

// ---------------------------------------------------------------- tracker_barrel: gun and sensitive

TEST_CASE("the gun start is checked against the barrel's world", "[spec11]")
{
  // outer radius 300.15 + 100 = 400.15 in x and y; half-length 500 + 100 = 600 in z
  auto at = [](double x, double y, double z) {
    json s = tracker();
    s["gun"]["position_mm"] = {x, y, z};
    return check(s);
  };
  CHECK(at(0, 0, 0).ok());
  CHECK(at(399, -399, 599).ok());
  CHECK(pointers(at(401, 0, 0)) == std::vector<std::string>{"/gun/position_mm"});
  CHECK(pointers(at(0, -401, 0)) == std::vector<std::string>{"/gun/position_mm"});
  CHECK(pointers(at(0, 0, 601)) == std::vector<std::string>{"/gun/position_mm"});
  CHECK(says(at(401, 0, 0), "/gun/position_mm", "400.15"));
  // direction rules are unchanged
  json s = tracker();
  s["gun"]["direction"] = {0, 0, 0};
  CHECK(pointers(check(s)) == std::vector<std::string>{"/gun/direction"});
}

TEST_CASE("the world follows the longest and the widest layer, not the last one", "[spec11]")
{
  json s = tracker();
  s["geometry"]["layers"][1]["half_length_mm"] = 2000;   // not the outermost layer
  s["gun"]["position_mm"] = {0, 0, 2050};
  CHECK(check(s).ok());                                  // |z| < 2000 + 100
  s["gun"]["position_mm"] = {0, 0, 2101};
  CHECK(pointers(check(s)) == std::vector<std::string>{"/gun/position_mm"});
}

TEST_CASE("sensitive depends on the template", "[spec11]")
{
  CHECK(check(with(tracker(), "/sensitive", json::array({"tracker_layer"}))).ok());
  ValidationResult r = check(with(tracker(), "/sensitive", json::array({"active"})));
  CHECK(pointers(r) == std::vector<std::string>{"/sensitive/0"});
  CHECK(says(r, "/sensitive/0", "must be 'tracker_layer'"));
  r = check(with(tracker(), "/sensitive", json::array({"tracker_layer", "tracker_layer"})));
  CHECK(pointers(r) == std::vector<std::string>{"/sensitive/1"});
  CHECK(pointers(check(with(tracker(), "/sensitive", json::array()))) == std::vector<std::string>{"/sensitive"});
  // the calorimeter does not accept the tracker's volume
  r = check(with(read_example("calorimeter.json"), "/sensitive", json::array({"tracker_layer"})));
  CHECK(pointers(r) == std::vector<std::string>{"/sensitive/0"});
  CHECK(says(r, "/sensitive/0", "must be 'absorber' or 'active'"));
}

// ---------------------------------------------------------------- field

TEST_CASE("the field block", "[spec11]")
{
  CHECK(check(with(tracker(), "/field", json::object())).ok());
  CHECK(check(without(tracker(), "/field")).ok());
  CHECK(check(with(tracker(), "/field/B_T", json::array({0, 0, 0}))).ok());
  CHECK(check(with(tracker(), "/field/B_T", json::array({-3, 4, 0}))).ok());
  CHECK(check(with(tracker(), "/field/B_T", json::array({6, 8, 0}))).ok());  // length exactly 10
  CHECK(check(with(read_example("calorimeter.json"), "/spec_version", "1.1")).ok());

  ValidationResult r = check(with(tracker(), "/field/B_T", json::array({6, 8, 0.001})));
  CHECK(pointers(r) == std::vector<std::string>{"/field/B_T"});
  CHECK(says(r, "/field/B_T", "at most 10 T"));
  r = check(with(tracker(), "/field/B_T", json::array({0, 0})));
  CHECK(pointers(r) == std::vector<std::string>{"/field/B_T"});
  r = check(with(tracker(), "/field/B_T", json::array({0, "1", 2})));
  CHECK(pointers(r) == std::vector<std::string>{"/field/B_T/1"});
  r = check(with(tracker(), "/field/B_T", 2));
  CHECK(pointers(r) == std::vector<std::string>{"/field/B_T"});
  r = check(with(tracker(), "/field", json::array({0, 0, 2})));
  CHECK(pointers(r) == std::vector<std::string>{"/field"});
  CHECK(says(r, "/field", "must be an object"));
  r = check(with(tracker(), "/field/Bz_T", 2));
  CHECK(pointers(r) == std::vector<std::string>{"/field/Bz_T"});
  CHECK(says(r, "/field/Bz_T", "did you mean 'B_T'"));
}

TEST_CASE("a field is checked after the gun and before the physics", "[spec11]")
{
  json s = tracker();
  s["field"]["B_T"] = {0, 0, 99};
  s["gun"]["energy_GeV"] = 0;
  s["physics"] = {{"list", "NOPE"}};
  CHECK(pointers(check(s)) == std::vector<std::string>{"/gun/energy_GeV", "/field/B_T", "/physics/list"});
}

// ---------------------------------------------------------------- canonical form

TEST_CASE("canonical form of a small tracker, with the defaults written out", "[canonical11]")
{
  const std::string expected =
      R"({"events":5,"field":{"B_T":[0,0,0]},"geometry":{"layers":[)"
      R"({"half_length_mm":300,"material":"G4_Si","radius_mm":50,"thickness_mm":0.5},)"
      R"({"half_length_mm":400,"material":"G4_Ge","radius_mm":100,"thickness_mm":0.5}]},)"
      R"("gun":{"direction":[1,0,0],"energy_GeV":1,"particle":"mu-","position_mm":[0,0,0]},)"
      R"("physics":{"list":"FTFP_BERT"},"seed":1,"sensitive":["tracker_layer"],)"
      R"("spec_version":"1.1","template":"tracker_barrel"})";
  REQUIRE(check(small_tracker()).ok());
  CHECK(canonical_spec(small_tracker()) == expected);
}

TEST_CASE("explicit defaults and omitted ones give the same canonical form", "[canonical11]")
{
  json s = small_tracker();
  s["geometry"]["layers"][0]["material"] = "G4_Si";
  s["gun"]["position_mm"] = {0, 0, 0};
  s["gun"]["direction"] = {7, 0, 0};
  s["field"] = {{"B_T", {0, 0, 0}}};
  s["sensitive"] = {"tracker_layer"};
  s["physics"] = {{"list", "FTFP_BERT"}};
  REQUIRE(check(s).ok());
  CHECK(canonical_spec(s) == canonical_spec(small_tracker()));
  // a field block with no B_T, and a field of negative zeros
  CHECK(canonical_spec(with(small_tracker(), "/field", json::object())) == canonical_spec(small_tracker()));
  CHECK(canonical_spec(with(small_tracker(), "/field/B_T", json::array({-0.0, 0.0, -0.0}))) ==
        canonical_spec(small_tracker()));
}

TEST_CASE("the layer order is kept, key order is not", "[canonical11]")
{
  json reversed = small_tracker();
  std::swap(reversed["geometry"]["layers"][0], reversed["geometry"]["layers"][1]);
  CHECK(check(reversed).issues.size() == 1);  // radii no longer increase: it is another, invalid, spec
  json reordered = json::parse(R"({"seed":1,"events":5,"gun":{"energy_GeV":1,"particle":"mu-"},
    "geometry":{"layers":[{"half_length_mm":300,"thickness_mm":0.5,"radius_mm":50},
                          {"material":"G4_Ge","radius_mm":100,"half_length_mm":400,"thickness_mm":0.5}]},
    "template":"tracker_barrel","spec_version":"1.1"})");
  CHECK(canonical_spec(reordered) == canonical_spec(small_tracker()));
}

TEST_CASE("a non-zero field is part of the canonical spec and of spec_oid", "[canonical11]")
{
  const json b0 = small_tracker();
  const json b2 = with(small_tracker(), "/field/B_T", json::array({0, 0, 2}));
  const json b2_other = with(small_tracker(), "/field/B_T", json::array({0, 0, 2.0000001}));
  const json b2_x = with(small_tracker(), "/field/B_T", json::array({2, 0, 0}));
  CHECK(canonical_spec(b2).find(R"("field":{"B_T":[0,0,2]})") != std::string::npos);
  const std::string o0 = oid_of_bytes(canonical_spec(b0));
  const std::string o2 = oid_of_bytes(canonical_spec(b2));
  CHECK(o0 != o2);
  CHECK(o2 != oid_of_bytes(canonical_spec(b2_other)));
  CHECK(o2 != oid_of_bytes(canonical_spec(b2_x)));
  CHECK(o2.size() == 28);
  CHECK(o2 == oid_of_bytes(canonical_spec(json::parse(canonical_spec(b2)))));  // idempotent
}

TEST_CASE("a 1.1 spec has a field in its canonical form, a 1.0 spec has none", "[canonical11]")
{
  const json cal10 = read_example("calorimeter.json");
  const json cal11 = with(cal10, "/spec_version", "1.1");
  const json c10 = json::parse(canonical_spec(cal10));
  const json c11 = json::parse(canonical_spec(cal11));
  CHECK_FALSE(c10.contains("field"));
  CHECK(c11.at("field") == json{{"B_T", {0, 0, 0}}});
  // apart from the version and the field the two are the same experiment
  json stripped = c11;
  stripped.erase("field");
  stripped["spec_version"] = "1.0";
  CHECK(stripped == c10);
  CHECK(oid_of_bytes(canonical_spec(cal10)) != oid_of_bytes(canonical_spec(cal11)));
  // calorimeter defaults are unchanged in 1.1
  CHECK(c11.at("gun").at("direction") == json::array({0, 0, 1}));
  CHECK(c11.at("gun").at("position_mm") == json::array({0, 0, -85}));
  CHECK(c11.at("sensitive") == json::array({"active"}));
}

TEST_CASE("spec 1.0 canonical form is the one that existed before 1.1", "[canonical11]")
{
  // The same string is pinned in test_spec.cc; here its OID, computed from the literal bytes.
  const std::string pinned =
      R"({"events":20,"geometry":{"absorber":{"material":"G4_Pb","thickness_mm":10},)"
      R"("active":{"material":"G4_lAr","thickness_mm":5},"layers":10,"transverse_mm":100},)"
      R"("gun":{"direction":[0,0,1],"energy_GeV":5,"particle":"e-","position_mm":[0,0,-85]},)"
      R"("physics":{"list":"FTFP_BERT"},"seed":42,"sensitive":["active"],)"
      R"("spec_version":"1.0","template":"layered_calorimeter"})";
  CHECK(canonical_spec(read_example("calorimeter.json")) == pinned);
  // recorded before spec 1.1 existed (tests/golden/SPEC10_OIDS.txt)
  CHECK(oid_of_bytes(pinned) == "8abf0a9df2f08bbd58d935e82ecb");
}

TEST_CASE("the tracker direction is normalised", "[canonical11]")
{
  const json s = with(small_tracker(), "/gun/direction", json::array({3, 4, 0}));
  CHECK(canonical_spec(s).find(R"("direction":[0.6,0.8,0])") != std::string::npos);
}
