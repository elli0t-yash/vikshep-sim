// Unit tests for the spec validator and the canonical form.

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "G4PhysListFactory.hh"
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

json example()
{
  std::ifstream in(std::string(VIKSHEP_EXAMPLES_DIR) + "/calorimeter.json");
  REQUIRE(in.good());
  return json::parse(in);
}

// Applies a JSON Pointer edit: sets (or, if value is discarded, removes).
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

const SpecIssue* find_issue(const ValidationResult& r, const std::string& pointer)
{
  for (const SpecIssue& i : r.issues) {
    if (i.pointer == pointer) {
      return &i;
    }
  }
  return nullptr;
}

bool message_has(const ValidationResult& r, const std::string& pointer, const std::string& text)
{
  const SpecIssue* i = find_issue(r, pointer);
  return i != nullptr && i->message.find(text) != std::string::npos;
}

ValidationResult check(const json& spec)
{
  return validate_spec(spec, catalogue());
}

}  // namespace

TEST_CASE("the example spec is valid", "[spec]")
{
  const ValidationResult r = check(example());
  CHECK(r.ok());
  CHECK(r.issues.empty());
}

TEST_CASE("optional fields may be given explicitly", "[spec]")
{
  json s = example();
  s["gun"]["position_mm"] = {1, 2, -50};
  s["gun"]["direction"] = {0, 0.5, 2};
  s["physics"] = {{"list", "QBBC"}};
  s["sensitive"] = {"absorber", "active"};
  CHECK(check(s).ok());
}

TEST_CASE("each required field missing is reported against its parent", "[spec]")
{
  const std::vector<std::pair<std::string, std::string>> required = {
      {"", "spec_version"},
      {"", "template"},
      {"", "geometry"},
      {"", "gun"},
      {"", "events"},
      {"", "seed"},
      {"/geometry", "layers"},
      {"/geometry", "absorber"},
      {"/geometry", "active"},
      {"/geometry", "transverse_mm"},
      {"/geometry/absorber", "material"},
      {"/geometry/absorber", "thickness_mm"},
      {"/geometry/active", "material"},
      {"/geometry/active", "thickness_mm"},
      {"/gun", "particle"},
      {"/gun", "energy_GeV"},
  };
  for (const auto& [parent, key] : required) {
    DYNAMIC_SECTION("missing " << parent << "/" << key)
    {
      const ValidationResult r = check(without(example(), parent + "/" + key));
      REQUIRE_FALSE(r.ok());
      CHECK(r.code == "SPEC_INVALID");
      CHECK(message_has(r, parent, "missing required field '" + key + "'"));
    }
  }
}

TEST_CASE("wrong types are reported at the field", "[spec]")
{
  const std::vector<std::pair<std::string, json>> cases = {
      {"/spec_version", 1.0},
      {"/template", 5},
      {"/geometry", "x"},
      {"/geometry/layers", "10"},
      {"/geometry/layers", 10.5},
      {"/geometry/absorber", 5},
      {"/geometry/absorber/material", 5},
      {"/geometry/absorber/thickness_mm", "10"},
      {"/geometry/active/thickness_mm", true},
      {"/geometry/transverse_mm", "100"},
      {"/gun", json::array()},
      {"/gun/particle", 7},
      {"/gun/energy_GeV", "5"},
      {"/gun/position_mm", {1, 2}},
      {"/gun/position_mm", "origin"},
      {"/gun/direction", "up"},
      {"/physics", 5},
      {"/sensitive", "active"},
      {"/events", "20"},
      {"/events", 20.5},
      {"/seed", 1.5},
      {"/seed", nullptr},
  };
  for (const auto& [pointer, value] : cases) {
    DYNAMIC_SECTION(pointer << " = " << value.dump())
    {
      const ValidationResult r = check(with(example(), pointer, value));
      REQUIRE_FALSE(r.ok());
      CHECK(find_issue(r, pointer) != nullptr);
    }
  }
}

TEST_CASE("wrong types inside arrays point at the element", "[spec]")
{
  ValidationResult r = check(with(example(), "/gun/position_mm", json::array({0, "y", 0})));
  CHECK(message_has(r, "/gun/position_mm/1", "must be a number"));
  r = check(with(example(), "/sensitive", json::array({"active", 3})));
  CHECK(find_issue(r, "/sensitive/1") != nullptr);
}

TEST_CASE("out-of-range values are rejected and boundaries accepted", "[spec]")
{
  struct Case {
    std::string pointer;
    json value;
    bool valid;
  };
  const std::vector<Case> cases = {
      {"/geometry/layers", 0, false},
      {"/geometry/layers", 1, true},
      {"/geometry/layers", 200, true},
      {"/geometry/layers", 201, false},
      {"/geometry/absorber/thickness_mm", 0, false},
      {"/geometry/absorber/thickness_mm", -1, false},
      {"/geometry/active/thickness_mm", 0.001, true},
      {"/geometry/transverse_mm", 0, false},
      {"/gun/energy_GeV", 0, false},
      {"/gun/energy_GeV", -5, false},
      {"/gun/energy_GeV", 10000, true},
      {"/gun/energy_GeV", 10000.5, false},
      {"/events", 0, false},
      {"/events", 1, true},
      {"/events", 10000000, true},
      {"/events", 10000001, false},
      {"/seed", -1, false},
      {"/seed", 0, true},
      {"/seed", 9007199254740991LL, true},
      {"/seed", 9007199254740992LL, false},
      {"/events", 20.0, true},
  };
  for (const Case& c : cases) {
    DYNAMIC_SECTION(c.pointer << " = " << c.value.dump())
    {
      const ValidationResult r = check(with(example(), c.pointer, c.value));
      CHECK(r.ok() == c.valid);
      if (!c.valid) {
        CHECK(find_issue(r, c.pointer) != nullptr);
      }
    }
  }
  const ValidationResult r = check(with(example(), "/gun/energy_GeV", 0));
  CHECK(message_has(r, "/gun/energy_GeV", "must be greater than 0"));
}

TEST_CASE("a zero direction is rejected", "[spec]")
{
  const ValidationResult r = check(with(example(), "/gun/direction", json::array({0, 0, 0})));
  REQUIRE_FALSE(r.ok());
  CHECK(message_has(r, "/gun/direction", "must not be all zero"));
  CHECK(check(with(example(), "/gun/direction", json::array({0, 0, -1}))).ok());
}

TEST_CASE("unknown fields are errors with a suggestion", "[spec]")
{
  ValidationResult r = check(with(example(), "/extra", 1));
  CHECK(message_has(r, "/extra", "unknown field"));

  json s = example();
  s["geometry"]["absorber"].erase("thickness_mm");
  s["geometry"]["absorber"]["thickness"] = 10;
  r = check(s);
  CHECK(message_has(r, "/geometry/absorber/thickness", "did you mean 'thickness_mm'?"));
  CHECK(message_has(r, "/geometry/absorber", "missing required field 'thickness_mm'"));

  r = check(with(example(), "/gun/energy", 5));
  CHECK(message_has(r, "/gun/energy", "did you mean 'energy_GeV'?"));

  r = check(with(example(), "/physics/lst", "QBBC"));
  CHECK(message_has(r, "/physics/lst", "did you mean 'list'?"));
}

TEST_CASE("pointers escape '/' and '~'", "[spec]")
{
  const ValidationResult r = check(with(example(), "/a~1b", 1));
  CHECK(find_issue(r, "/a~1b") != nullptr);
  json s = example();
  s["x~y"] = 1;
  CHECK(find_issue(check(s), "/x~0y") != nullptr);
}

TEST_CASE("names are checked against Geant4 and the particle list", "[spec][catalogue]")
{
  ValidationResult r = check(with(example(), "/geometry/absorber/material", "G4_Pbb"));
  CHECK(message_has(r, "/geometry/absorber/material", "did you mean 'G4_Pb'?"));
  r = check(with(example(), "/geometry/active/material", "lead"));
  CHECK(find_issue(r, "/geometry/active/material") != nullptr);

  r = check(with(example(), "/physics/list", "FTFP_BRT"));
  CHECK(message_has(r, "/physics/list", "did you mean 'FTFP_BERT'?"));
  r = check(with(example(), "/physics/list", "NOT_A_LIST"));
  CHECK(find_issue(r, "/physics/list") != nullptr);

  r = check(with(example(), "/gun/particle", "electron"));
  CHECK(message_has(r, "/gun/particle", "unknown particle"));
  for (const std::string& p : allowed_particles()) {
    CHECK(check(with(example(), "/gun/particle", p)).ok());
  }
}

TEST_CASE("physics list names agree with G4PhysListFactory", "[spec][catalogue]")
{
  G4PhysListFactory factory;
  std::vector<std::string> names;
  for (const std::string& list : catalogue().physics_lists) {
    names.push_back(list);
    for (const std::string& em : catalogue().em_options) {
      names.push_back(list + em);
    }
  }
  names.push_back("FTFP_BERT_NOPE");
  names.push_back("_EMZ");
  names.push_back("");
  for (const std::string& name : names) {
    const bool ours = check(with(example(), "/physics/list", name)).ok();
    CHECK(ours == factory.IsReferencePhysList(name));
  }
}

TEST_CASE("sensitive must be a non-empty set of slabs", "[spec]")
{
  CHECK(check(with(example(), "/sensitive", json::array({"absorber"}))).ok());
  CHECK(check(with(example(), "/sensitive", json::array({"absorber", "active"}))).ok());
  CHECK(message_has(check(with(example(), "/sensitive", json::array())), "/sensitive",
                    "at least one"));
  CHECK(message_has(check(with(example(), "/sensitive", json::array({"active", "active"}))),
                    "/sensitive/1", "duplicate"));
  CHECK(find_issue(check(with(example(), "/sensitive", json::array({"gap"}))), "/sensitive/0") !=
        nullptr);
}

TEST_CASE("several errors are reported at once, in a fixed order", "[spec]")
{
  json s = example();
  s["geometry"]["layers"] = 0;
  s["geometry"]["absorber"]["thickness_mm"] = -1;
  s["gun"]["energy_GeV"] = 20000;
  s["events"] = "many";
  s.erase("seed");
  s["bogus"] = true;
  const ValidationResult r = check(s);
  REQUIRE_FALSE(r.ok());
  std::vector<std::string> pointers;
  for (const SpecIssue& i : r.issues) {
    pointers.push_back(i.pointer);
  }
  const std::vector<std::string> expected = {"/geometry/layers", "/geometry/absorber/thickness_mm",
                                             "/gun/energy_GeV", "/events", "", "/bogus"};
  CHECK(pointers == expected);
}

TEST_CASE("an unsupported spec_version stops at that field", "[spec]")
{
  json s = with(example(), "/spec_version", "2.0");
  s["geometry"] = "garbage";
  const ValidationResult r = check(s);
  CHECK(r.code == "SPEC_VERSION_UNSUPPORTED");
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].pointer == "/spec_version");
}

TEST_CASE("the root must be an object", "[spec]")
{
  const ValidationResult r = check(json::array());
  CHECK(r.code == "SPEC_INVALID");
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].pointer == "");
}

TEST_CASE("issues_to_json has sorted keys", "[spec]")
{
  const json j = issues_to_json({{"/a", "m"}});
  CHECK(j.dump() == R"([{"message":"m","pointer":"/a"}])");
}

// ---- canonical form ----

TEST_CASE("canonical form of the example", "[canonical]")
{
  const std::string expected =
      R"({"events":20,"geometry":{"absorber":{"material":"G4_Pb","thickness_mm":10},)"
      R"("active":{"material":"G4_lAr","thickness_mm":5},"layers":10,"transverse_mm":100},)"
      R"("gun":{"direction":[0,0,1],"energy_GeV":5,"particle":"e-","position_mm":[0,0,-20]},)"
      R"("physics":{"list":"FTFP_BERT"},"seed":42,"sensitive":["active"],)"
      R"("spec_version":"1.0","template":"layered_calorimeter"})";
  CHECK(canonical_spec(example()) == expected);
}

TEST_CASE("canonical form ignores key order", "[canonical]")
{
  const json reordered = json::parse(R"({
    "seed": 42, "events": 20,
    "gun": {"energy_GeV": 5, "particle": "e-"},
    "geometry": {"transverse_mm": 100,
                 "active": {"thickness_mm": 5, "material": "G4_lAr"},
                 "absorber": {"thickness_mm": 10, "material": "G4_Pb"},
                 "layers": 10},
    "template": "layered_calorimeter", "spec_version": "1.0"})");
  REQUIRE(check(reordered).ok());
  CHECK(canonical_spec(reordered) == canonical_spec(example()));
}

TEST_CASE("canonical form treats explicit defaults like omitted ones", "[canonical]")
{
  json s = example();
  s["gun"]["position_mm"] = {0, 0, -20};
  s["gun"]["direction"] = {0, 0, 7};
  s["physics"] = {{"list", "FTFP_BERT"}};
  s["sensitive"] = {"active"};
  REQUIRE(check(s).ok());
  CHECK(canonical_spec(s) == canonical_spec(example()));
}

TEST_CASE("canonical form treats 5 and 5.0 alike", "[canonical]")
{
  json s = with(example(), "/gun/energy_GeV", 5.0);
  s["events"] = 20.0;
  s["geometry"]["layers"] = 10.0;
  REQUIRE(check(s).ok());
  CHECK(canonical_spec(s) == canonical_spec(example()));
}

TEST_CASE("canonical form sorts sensitive", "[canonical]")
{
  const json a = with(example(), "/sensitive", json::array({"active", "absorber"}));
  const json b = with(example(), "/sensitive", json::array({"absorber", "active"}));
  CHECK(canonical_spec(a) == canonical_spec(b));
  CHECK(canonical_spec(a).find(R"("sensitive":["absorber","active"])") != std::string::npos);
}

TEST_CASE("canonical form normalises the direction", "[canonical]")
{
  const json s = with(example(), "/gun/direction", json::array({3, 0, 4}));
  CHECK(canonical_spec(s).find(R"("direction":[0.6,0,0.8])") != std::string::npos);
  // Different lengths, same direction, same bytes.
  const json t = with(example(), "/gun/direction", json::array({6, 0, 8}));
  CHECK(canonical_spec(s) == canonical_spec(t));
}

TEST_CASE("canonical form differs when the experiment differs", "[canonical]")
{
  CHECK(canonical_spec(with(example(), "/seed", 43)) != canonical_spec(example()));
  CHECK(canonical_spec(with(example(), "/gun/particle", "e+")) != canonical_spec(example()));
}

TEST_CASE("canonical numbers follow the ECMAScript format", "[canonical]")
{
  const std::vector<std::pair<json, std::string>> cases = {
      {0.1, "0.1"},
      {100.0, "100"},
      {1e-7, "1e-7"},
      {0.000001, "0.000001"},
      {1e21, "1e+21"},
      {1e20, "100000000000000000000"},
      {1.5e300, "1.5e+300"},
      {123.456, "123.456"},
      {2.5e-5, "0.000025"},
  };
  for (const auto& [value, text] : cases) {
    DYNAMIC_SECTION(value.dump() << " -> " << text)
    {
      const std::string out = canonical_spec(with(example(), "/geometry/transverse_mm", value));
      CHECK(out.find("\"transverse_mm\":" + text + "}") != std::string::npos);
    }
  }
}

TEST_CASE("canonical form is idempotent and valid", "[canonical]")
{
  const std::string once = canonical_spec(example());
  const json parsed = json::parse(once);
  CHECK(check(parsed).ok());
  CHECK(canonical_spec(parsed) == once);
}
