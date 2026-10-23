#include "spec_validate.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "versions.hh"

using nlohmann::json;

namespace {

constexpr double kSeedMax = 9007199254740991.0;  // 2^53 - 1
constexpr double kEventsMax = 10000000.0;
constexpr double kLayersMax = 200.0;
constexpr double kEnergyMaxGeV = 10000.0;

// RFC 6901: "~" becomes "~0" and "/" becomes "~1".
std::string escape_token(const std::string& token)
{
  std::string out;
  for (char c : token) {
    if (c == '~') {
      out += "~0";
    }
    else if (c == '/') {
      out += "~1";
    }
    else {
      out += c;
    }
  }
  return out;
}

std::string child(const std::string& pointer, const std::string& key)
{
  return pointer + "/" + escape_token(key);
}

std::string child(const std::string& pointer, std::size_t index)
{
  return pointer + "/" + std::to_string(index);
}

std::size_t edit_distance(const std::string& a, const std::string& b)
{
  std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) {
    prev[j] = j;
  }
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
    }
    prev.swap(cur);
  }
  return prev[b.size()];
}

// The candidate that is close to name (small edit distance, or one is a prefix
// of the other), or an empty string.
std::string closest(const std::string& name, const std::vector<std::string>& candidates)
{
  const std::size_t limit = name.size() >= 5 ? 2 : 1;
  std::string best;
  std::size_t best_distance = limit + 1;
  for (const std::string& c : candidates) {
    std::size_t d = edit_distance(name, c);
    if (!name.empty() && (c.compare(0, name.size(), name) == 0 ||
                          name.compare(0, c.size(), c) == 0)) {
      d = std::min<std::size_t>(d, limit);
    }
    if (d < best_distance) {
      best_distance = d;
      best = c;
    }
  }
  return best;
}

std::string quote(const std::string& s)
{
  return "'" + s + "'";
}

std::string join(const std::vector<std::string>& items)
{
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    out += (i == 0 ? "" : ", ") + items[i];
  }
  return out;
}

// A JSON number that has an integer value (42 and 42.0 both qualify), as in
// JSON Schema.
bool is_integral(const json& j)
{
  if (j.is_number_integer()) {
    return true;
  }
  if (j.is_number_float()) {
    const double v = j.get<double>();
    return std::isfinite(v) && std::floor(v) == v;
  }
  return false;
}

class Checker {
 public:
  explicit Checker(const Catalogue& catalogue) : catalogue_(catalogue) {}

  std::vector<SpecIssue> issues;

  void add(const std::string& pointer, const std::string& message)
  {
    issues.push_back({pointer, message, ""});
  }

  // Reports a field that is not in allowed.
  void check_unknown(const json& obj, const std::string& pointer,
                     const std::vector<std::string>& allowed)
  {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
      if (std::find(allowed.begin(), allowed.end(), it.key()) != allowed.end()) {
        continue;
      }
      const std::string guess = closest(it.key(), allowed);
      if (!guess.empty()) {
        add(child(pointer, it.key()), "unknown field; did you mean " + quote(guess) + "?");
      }
      else {
        add(child(pointer, it.key()), "unknown field; allowed fields: " + join(allowed));
      }
    }
  }

  // The member key of obj, or nullptr (and a report against obj) when it is
  // missing and required.
  const json* member(const json& obj, const std::string& pointer, const std::string& key,
                     bool required)
  {
    auto it = obj.find(key);
    if (it == obj.end()) {
      if (required) {
        issues.push_back({pointer, "missing required field " + quote(key), key});
      }
      return nullptr;
    }
    return &*it;
  }

  // An object member that must itself be an object.
  const json* object_member(const json& obj, const std::string& pointer, const std::string& key,
                            bool required)
  {
    const json* m = member(obj, pointer, key, required);
    if (m == nullptr) {
      return nullptr;
    }
    if (!m->is_object()) {
      add(child(pointer, key), "must be an object");
      return nullptr;
    }
    return m;
  }

  bool number(const json& j, const std::string& pointer, double& out)
  {
    if (!j.is_number()) {
      add(pointer, "must be a number");
      return false;
    }
    out = j.get<double>();
    return true;
  }

  bool integer_in_range(const json& j, const std::string& pointer, double lo, double hi)
  {
    if (!is_integral(j)) {
      add(pointer, "must be an integer from " + format(lo) + " to " + format(hi));
      return false;
    }
    const double v = j.get<double>();
    if (v < lo || v > hi) {
      add(pointer, "must be an integer from " + format(lo) + " to " + format(hi));
      return false;
    }
    return true;
  }

  // A number greater than 0 (and at most max, if max > 0).
  void positive(const json& j, const std::string& pointer, double max = 0.0)
  {
    double v = 0.0;
    if (!number(j, pointer, v)) {
      return;
    }
    if (!(v > 0.0)) {
      add(pointer, "must be greater than 0");
    }
    else if (max > 0.0 && v > max) {
      add(pointer, "must be at most " + format(max));
    }
  }

  // Three finite numbers. Returns true and the values when well formed.
  bool vector3(const json& j, const std::string& pointer, double out[3])
  {
    if (!j.is_array() || j.size() != 3) {
      add(pointer, "must be an array of 3 numbers [x, y, z]");
      return false;
    }
    bool ok = true;
    for (std::size_t i = 0; i < 3; ++i) {
      if (!j[i].is_number()) {
        add(child(pointer, i), "must be a number");
        ok = false;
      }
      else {
        out[i] = j[i].get<double>();
      }
    }
    return ok;
  }

  void check_string_name(const json& j, const std::string& pointer, const std::string& what,
                         const std::vector<std::string>& known,
                         bool (*is_known)(const std::string&, const Catalogue&),
                         const std::string& allowed_text)
  {
    if (!j.is_string()) {
      add(pointer, "must be a string");
      return;
    }
    const std::string name = j.get<std::string>();
    if (is_known(name, catalogue_)) {
      return;
    }
    const std::string guess = closest(name, known);
    std::string message = "unknown " + what + " " + quote(name);
    if (!guess.empty()) {
      message += "; did you mean " + quote(guess) + "?";
    }
    else if (!allowed_text.empty()) {
      message += "; " + allowed_text;
    }
    add(pointer, message);
  }

  void check_slab(const json& obj, const std::string& pointer)
  {
    if (const json* m = member(obj, pointer, "material", true)) {
      check_string_name(
          *m, child(pointer, "material"), "material", catalogue_.materials,
          [](const std::string& n, const Catalogue& c) {
            return std::find(c.materials.begin(), c.materials.end(), n) != c.materials.end();
          },
          "use a Geant4 NIST material name such as G4_Pb or G4_lAr");
    }
    if (const json* m = member(obj, pointer, "thickness_mm", true)) {
      positive(*m, child(pointer, "thickness_mm"));
    }
    check_unknown(obj, pointer, {"material", "thickness_mm"});
  }

  void check_geometry(const json& obj, const std::string& pointer)
  {
    if (const json* m = member(obj, pointer, "layers", true)) {
      integer_in_range(*m, child(pointer, "layers"), 1, kLayersMax);
    }
    for (const char* slab : {"absorber", "active"}) {
      if (const json* m = object_member(obj, pointer, slab, true)) {
        check_slab(*m, child(pointer, slab));
      }
    }
    if (const json* m = member(obj, pointer, "transverse_mm", true)) {
      positive(*m, child(pointer, "transverse_mm"));
    }
    check_unknown(obj, pointer, {"layers", "absorber", "active", "transverse_mm"});
  }

  void check_gun(const json& obj, const std::string& pointer)
  {
    if (const json* m = member(obj, pointer, "particle", true)) {
      check_string_name(
          *m, child(pointer, "particle"), "particle", allowed_particles(),
          [](const std::string& n, const Catalogue&) {
            const auto& p = allowed_particles();
            return std::find(p.begin(), p.end(), n) != p.end();
          },
          "allowed: " + join(allowed_particles()));
    }
    if (const json* m = member(obj, pointer, "energy_GeV", true)) {
      positive(*m, child(pointer, "energy_GeV"), kEnergyMaxGeV);
    }
    if (const json* m = member(obj, pointer, "position_mm", false)) {
      double v[3];
      vector3(*m, child(pointer, "position_mm"), v);
    }
    if (const json* m = member(obj, pointer, "direction", false)) {
      double v[3];
      if (vector3(*m, child(pointer, "direction"), v)) {
        const double norm = std::hypot(std::hypot(v[0], v[1]), v[2]);
        if (norm == 0.0) {
          add(child(pointer, "direction"),
              "must not be all zero; give a direction such as [0, 0, 1]");
        }
        else if (!std::isfinite(norm)) {
          add(child(pointer, "direction"), "components are too large; use values below 1e300");
        }
      }
    }
    check_unknown(obj, pointer, {"particle", "energy_GeV", "position_mm", "direction"});
  }

  // The gun start must lie strictly inside the world (docs/SPEC.md, 2.2).
  void check_gun_in_world(const json& spec)
  {
    CalorimeterGeometry g;
    if (!read_geometry(spec, g) || !spec.at("gun").is_object()) {
      return;
    }
    auto it = spec.at("gun").find("position_mm");
    if (it == spec.at("gun").end() || !it->is_array() || it->size() != 3) {
      return;
    }
    double p[3];
    for (std::size_t i = 0; i < 3; ++i) {
      if (!(*it)[i].is_number()) {
        return;
      }
      p[i] = (*it)[i].get<double>();
    }
    const double hx = 0.5 * g.world_transverse_mm();
    const double hz = 0.5 * g.world_length_mm();
    if (!(std::fabs(p[0]) < hx && std::fabs(p[1]) < hx && std::fabs(p[2]) < hz)) {
      add("/gun/position_mm", "must be inside the world: |x| < " + number_text(hx) +
                                  " mm, |y| < " + number_text(hx) + " mm, |z| < " +
                                  number_text(hz) + " mm for this geometry");
    }
  }

  void check_physics(const json& obj, const std::string& pointer)
  {
    if (const json* m = member(obj, pointer, "list", false)) {
      check_string_name(
          *m, child(pointer, "list"), "physics list", catalogue_.physics_lists,
          [](const std::string& n, const Catalogue& c) { return is_physics_list(n, c); },
          "run 'vikshep-sim info --json' to see physics_lists");
    }
    check_unknown(obj, pointer, {"list"});
  }

  void check_sensitive(const json& j, const std::string& pointer)
  {
    if (!j.is_array()) {
      add(pointer, "must be an array of 'absorber' and/or 'active'");
      return;
    }
    if (j.empty()) {
      add(pointer, "must list at least one of 'absorber', 'active'");
      return;
    }
    std::vector<std::string> seen;
    for (std::size_t i = 0; i < j.size(); ++i) {
      const std::string p = child(pointer, i);
      if (!j[i].is_string() || (j[i] != "absorber" && j[i] != "active")) {
        add(p, "must be 'absorber' or 'active'");
        continue;
      }
      const std::string name = j[i].get<std::string>();
      if (std::find(seen.begin(), seen.end(), name) != seen.end()) {
        add(p, "duplicate entry " + quote(name) + "; list each slab once");
      }
      seen.push_back(name);
    }
  }

  void check_root(const json& spec)
  {
    if (const json* m = member(spec, "", "spec_version", true)) {
      if (!m->is_string()) {
        add("/spec_version", "must be the string \"" + std::string(kSpecVersion) + "\"");
      }
    }
    if (const json* m = member(spec, "", "template", true)) {
      if (!m->is_string() || m->get<std::string>() != kTemplateLayeredCalorimeter) {
        add("/template", "must be \"" + std::string(kTemplateLayeredCalorimeter) +
                             "\", the only template in spec 1.0");
      }
    }
    if (const json* m = object_member(spec, "", "geometry", true)) {
      check_geometry(*m, "/geometry");
    }
    if (const json* m = object_member(spec, "", "gun", true)) {
      check_gun(*m, "/gun");
    }
    if (spec.contains("geometry") && spec.contains("gun") && spec.at("geometry").is_object() &&
        spec.at("gun").is_object()) {
      check_gun_in_world(spec);
    }
    if (const json* m = object_member(spec, "", "physics", false)) {
      check_physics(*m, "/physics");
    }
    if (const json* m = member(spec, "", "sensitive", false)) {
      check_sensitive(*m, "/sensitive");
    }
    if (const json* m = member(spec, "", "events", true)) {
      integer_in_range(*m, "/events", 1, kEventsMax);
    }
    if (const json* m = member(spec, "", "seed", true)) {
      integer_in_range(*m, "/seed", 0, kSeedMax);
    }
    check_unknown(spec, "",
                  {"spec_version", "template", "geometry", "gun", "physics", "sensitive", "events",
                   "seed"});
  }

 private:
  static std::string number_text(double v)
  {
    std::ostringstream os;
    os << std::setprecision(10) << v;
    return os.str();
  }

  static std::string format(double v)
  {
    // Whole numbers only: bounds in messages are integers.
    return std::to_string(static_cast<long long>(v));
  }

  // A reference physics list, optionally followed by an EM option suffix.
  static bool is_physics_list(const std::string& name, const Catalogue& c)
  {
    const auto& lists = c.physics_lists;
    if (std::find(lists.begin(), lists.end(), name) != lists.end()) {
      return true;
    }
    for (const std::string& suffix : c.em_options) {
      if (name.size() > suffix.size() &&
          name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const std::string base = name.substr(0, name.size() - suffix.size());
        if (std::find(lists.begin(), lists.end(), base) != lists.end()) {
          return true;
        }
      }
    }
    return false;
  }

  const Catalogue& catalogue_;
};

}  // namespace

const std::vector<std::string>& allowed_particles()
{
  static const std::vector<std::string> particles = {
      "e-", "e+", "gamma", "mu-", "mu+", "pi-", "pi+", "proton", "neutron", "kaon+", "kaon-"};
  return particles;
}

bool read_geometry(const json& spec, CalorimeterGeometry& out)
{
  auto geometry = spec.find("geometry");
  if (geometry == spec.end() || !geometry->is_object()) {
    return false;
  }
  auto layers = geometry->find("layers");
  auto transverse = geometry->find("transverse_mm");
  if (layers == geometry->end() || transverse == geometry->end() || !is_integral(*layers) ||
      !transverse->is_number()) {
    return false;
  }
  double thickness[2];
  int n = 0;
  for (const char* slab : {"absorber", "active"}) {
    auto s = geometry->find(slab);
    if (s == geometry->end() || !s->is_object()) {
      return false;
    }
    auto t = s->find("thickness_mm");
    if (t == s->end() || !t->is_number()) {
      return false;
    }
    thickness[n++] = t->get<double>();
  }
  const double l = layers->get<double>();
  const double w = transverse->get<double>();
  if (l < 1 || l > kLayersMax || !(thickness[0] > 0) || !(thickness[1] > 0) || !(w > 0)) {
    return false;
  }
  out.layers = static_cast<int>(l);
  out.absorber_thickness_mm = thickness[0];
  out.active_thickness_mm = thickness[1];
  out.transverse_mm = w;
  return true;
}

ValidationResult validate_spec(const json& spec, const Catalogue& catalogue)
{
  ValidationResult result;
  Checker checker(catalogue);

  if (!spec.is_object()) {
    checker.add("", "the spec must be a JSON object");
    result.code = "SPEC_INVALID";
    result.issues = checker.issues;
    return result;
  }

  // A version we do not know has rules we do not know: stop here.
  auto version = spec.find("spec_version");
  if (version != spec.end() && version->is_string() &&
      version->get<std::string>() != kSpecVersion) {
    result.code = "SPEC_VERSION_UNSUPPORTED";
    result.issues.push_back({"/spec_version",
                             "unsupported spec_version " + quote(version->get<std::string>()) +
                                 "; this build accepts \"" + kSpecVersion + "\"",
                             ""});
    return result;
  }

  checker.check_root(spec);
  if (!checker.issues.empty()) {
    result.code = "SPEC_INVALID";
    result.issues = checker.issues;
  }
  return result;
}

json issues_to_json(const std::vector<SpecIssue>& issues)
{
  json out = json::array();
  for (const SpecIssue& i : issues) {
    json entry = {{"message", i.message}, {"pointer", i.pointer}};
    if (!i.missing.empty()) {
      entry["missing"] = i.missing;
    }
    out.push_back(entry);
  }
  return out;
}
