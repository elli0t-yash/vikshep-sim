#include "export3d.hh"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "calorimeter_geometry.hh"
#include "errors.hh"
#include "sha3_256.hh"
#include "spec_canonical.hh"
#include "tracker_geometry.hh"
#include "versions.hh"

using nlohmann::json;

namespace {

// Test only: VIKSHEP_TEST_EXPORT_FAIL=1 makes the export fail, to test that this is an INTERNAL
// error with no output folder (tests/).
void test_export_failure()
{
  const char* value = std::getenv("VIKSHEP_TEST_EXPORT_FAIL");
  if (value != nullptr && std::string(value) == "1") {
    throw ExportError("deliberate failure (VIKSHEP_TEST_EXPORT_FAIL)");
  }
}

ExportedFile describe_file(const std::string& path, const std::string& name)
{
  try {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
      throw ExportError("cannot read back " + name);
    }
    ExportedFile file;
    file.name = name;
    file.bytes = static_cast<std::uint64_t>(in.tellg());
    file.oid = oid_of(sha3_256_of_file(path));
    return file;
  }
  catch (const ExportError&) {
    throw;
  }
  catch (const std::exception& e) {
    throw ExportError(std::string("cannot hash ") + name + ": " + e.what());
  }
}

std::string base_name(const std::string& path)
{
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

// ------------------------------------------------------------------ trajectories.json

EventTracks select_tracks(std::vector<TrackRecord> eligible, std::uint32_t tracks_total)
{
  const std::size_t cap = static_cast<std::size_t>(kExportMaxTracksPerEvent);
  if (eligible.size() > cap) {
    std::stable_sort(eligible.begin(), eligible.end(), [](const TrackRecord& a, const TrackRecord& b) {
      const bool pa = a.parent_id == 0;
      const bool pb = b.parent_id == 0;
      if (pa != pb) {
        return pa;  // primaries first
      }
      if (pa) {
        return a.track_id < b.track_id;
      }
      if (a.kinetic_energy_start_gev != b.kinetic_energy_start_gev) {
        return a.kinetic_energy_start_gev > b.kinetic_energy_start_gev;
      }
      return a.track_id < b.track_id;
    });
    eligible.resize(cap);
  }
  std::sort(eligible.begin(), eligible.end(),
            [](const TrackRecord& a, const TrackRecord& b) { return a.track_id < b.track_id; });
  EventTracks result;
  result.tracks_total = tracks_total;
  result.tracks = std::move(eligible);
  return result;
}

std::string format_fixed(double value, int decimals)
{
  if (!std::isfinite(value)) {
    throw ExportError("a value to export is not finite");
  }
  char buffer[64];
  const int n = std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
  if (n < 0 || n >= static_cast<int>(sizeof buffer)) {
    throw ExportError("a value to export is too large to format");
  }
  std::string text(buffer, static_cast<std::size_t>(n));
  if (text[0] == '-' && text.find_first_not_of("-0.") == std::string::npos) {
    text.erase(0, 1);  // "-0.000" -> "0.000"
  }
  return text;
}

std::string trajectories_header(const std::string& spec_oid, const ExportOptions& options)
{
  return std::string("{\"format_version\":\"") + kExportFormatVersion + "\",\"unit\":\"mm\",\"spec_oid\":\"" +
         spec_oid + "\",\"min_energy_GeV\":" + format_fixed(options.min_energy_gev, 6) +
         ",\"max_tracks_per_event\":" + std::to_string(kExportMaxTracksPerEvent) +
         ",\"events_requested\":" + std::to_string(options.events) + ",\"events\":[\n";
}

std::string trajectories_event_json(std::uint64_t event_id, const EventTracks& event)
{
  std::string out = "{\"event_id\":" + std::to_string(event_id) +
                    ",\"tracks_total\":" + std::to_string(event.tracks_total) +
                    ",\"dropped_tracks\":" + std::to_string(event.dropped()) + ",\"tracks\":[";
  bool first_track = true;
  for (const TrackRecord& t : event.tracks) {
    if (!first_track) {
      out += ',';
    }
    first_track = false;
    out += "{\"track_id\":" + std::to_string(t.track_id) + ",\"parent_id\":" + std::to_string(t.parent_id) +
           ",\"pdg\":" + std::to_string(t.pdg) + ",\"charge\":" + std::to_string(t.charge) +
           ",\"kinetic_energy_start_GeV\":" + format_fixed(t.kinetic_energy_start_gev, 6) + ",\"points\":[";
    bool first_point = true;
    for (const auto& p : t.points) {
      if (!first_point) {
        out += ',';
      }
      first_point = false;
      out += '[' + format_fixed(p[0], 3) + ',' + format_fixed(p[1], 3) + ',' + format_fixed(p[2], 3) + ']';
    }
    out += "]}";
  }
  out += "]}";
  return out;
}

TrajectoryWriter::TrajectoryWriter(const std::string& path, const std::string& spec_oid,
                                   const ExportOptions& options)
    : path_(path), out_(path, std::ios::binary | std::ios::trunc)
{
  test_export_failure();
  if (!out_) {
    throw ExportError("cannot create " + std::string(kTrajectoriesFileName));
  }
  out_ << trajectories_header(spec_oid, options);
  if (!out_) {
    throw ExportError("cannot write " + std::string(kTrajectoriesFileName));
  }
}

void TrajectoryWriter::add_event(std::uint64_t event_id, const EventTracks& event)
{
  if (events_ > 0) {
    out_ << ",\n";
  }
  out_ << trajectories_event_json(event_id, event);
  ++events_;
  if (!out_) {
    throw ExportError("cannot write " + std::string(kTrajectoriesFileName));
  }
}

ExportedFile TrajectoryWriter::finish()
{
  out_ << "\n]}\n";
  out_.flush();
  if (!out_) {
    throw ExportError("cannot write " + std::string(kTrajectoriesFileName));
  }
  out_.close();
  return describe_file(path_, kTrajectoriesFileName);
}

// ------------------------------------------------------------------ geometry.glb

namespace {

constexpr double kMetresPerMm = 0.001;
constexpr double kPi = 3.14159265358979323846;

struct MeshData {
  std::string name;
  std::vector<float> positions;  // x, y, z in metres
  std::vector<float> normals;
  std::vector<std::uint16_t> indices;
  int material = 0;
};

void add_vertex(MeshData& m, double x_mm, double y_mm, double z_mm, double nx, double ny, double nz)
{
  m.positions.push_back(static_cast<float>(x_mm * kMetresPerMm));
  m.positions.push_back(static_cast<float>(y_mm * kMetresPerMm));
  m.positions.push_back(static_cast<float>(z_mm * kMetresPerMm));
  m.normals.push_back(static_cast<float>(nx));
  m.normals.push_back(static_cast<float>(ny));
  m.normals.push_back(static_cast<float>(nz));
}

void add_triangle(MeshData& m, int a, int b, int c)
{
  m.indices.push_back(static_cast<std::uint16_t>(a));
  m.indices.push_back(static_cast<std::uint16_t>(b));
  m.indices.push_back(static_cast<std::uint16_t>(c));
}

// A box centred on the origin (docs/EXPORT3D.md, 2.3): faces +x, -x, +y, -y, +z, -z, 4 vertices each.
MeshData make_box(const std::string& name, int material, double hx, double hy, double hz)
{
  MeshData m;
  m.name = name;
  m.material = material;
  struct Face {
    std::array<int, 3> n, t1, t2;  // t1 x t2 = n
  };
  const Face faces[6] = {
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},   {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
      {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},   {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
      {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},   {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
  };
  const double half[3] = {hx, hy, hz};
  for (const Face& f : faces) {
    const int base = static_cast<int>(m.positions.size() / 3);
    const int signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (const auto& s : signs) {
      double p[3];
      for (int axis = 0; axis < 3; ++axis) {
        p[axis] = f.n[axis] * half[axis] + s[0] * f.t1[axis] * half[axis] + s[1] * f.t2[axis] * half[axis];
      }
      add_vertex(m, p[0], p[1], p[2], f.n[0], f.n[1], f.n[2]);
    }
    add_triangle(m, base, base + 1, base + 2);
    add_triangle(m, base, base + 2, base + 3);
  }
  return m;
}

// A hollow tube along z, kTubeSegments segments (docs/EXPORT3D.md, 2.3): outer wall, inner wall,
// cap at +z, cap at -z; each part has its own vertices.
MeshData make_tube(const std::string& name, int material, double inner_mm, double outer_mm, double half_length_mm)
{
  MeshData m;
  m.name = name;
  m.material = material;
  const int s = kTubeSegments;
  auto cosine = [&](int k) { return std::cos(2.0 * kPi * k / s); };
  auto sine = [&](int k) { return std::sin(2.0 * kPi * k / s); };

  // outer wall (2 rings: z = -half_length, +half_length), then inner wall
  for (const bool outer : {true, false}) {
    const double radius = outer ? outer_mm : inner_mm;
    const double sign = outer ? 1.0 : -1.0;
    const int base = static_cast<int>(m.positions.size() / 3);
    for (const double z : {-half_length_mm, half_length_mm}) {
      for (int k = 0; k < s; ++k) {
        add_vertex(m, radius * cosine(k), radius * sine(k), z, sign * cosine(k), sign * sine(k), 0.0);
      }
    }
    for (int k = 0; k < s; ++k) {
      const int k1 = (k + 1) % s;
      const int a = base + k, b = base + k1, c = base + s + k1, d = base + s + k;
      if (outer) {
        add_triangle(m, a, b, c);
        add_triangle(m, a, c, d);
      }
      else {
        add_triangle(m, a, c, b);
        add_triangle(m, a, d, c);
      }
    }
  }
  // end caps: +z then -z; vertices: ring of outer radius, then ring of inner radius
  for (const bool plus : {true, false}) {
    const double z = plus ? half_length_mm : -half_length_mm;
    const double nz = plus ? 1.0 : -1.0;
    const int base = static_cast<int>(m.positions.size() / 3);
    for (const double radius : {outer_mm, inner_mm}) {
      for (int k = 0; k < s; ++k) {
        add_vertex(m, radius * cosine(k), radius * sine(k), z, 0.0, 0.0, nz);
      }
    }
    for (int k = 0; k < s; ++k) {
      const int k1 = (k + 1) % s;
      const int o0 = base + k, o1 = base + k1, i0 = base + s + k, i1 = base + s + k1;
      if (plus) {
        add_triangle(m, o0, o1, i1);
        add_triangle(m, o0, i1, i0);
      }
      else {
        add_triangle(m, o0, i1, o1);
        add_triangle(m, o0, i0, i1);
      }
    }
  }
  return m;
}

// A float written in the shortest decimal form that reads back to the same float32.
json float_json(float value)
{
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
  return std::strtod(std::string(buffer, result.ptr).c_str(), nullptr);
}

void put_u32(std::string& out, std::uint32_t v)
{
  for (int i = 0; i < 4; ++i) {
    out += static_cast<char>((v >> (8 * i)) & 0xFF);
  }
}

void put_float(std::string& out, float f)
{
  std::uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof bits);
  put_u32(out, bits);
}

void put_u16(std::string& out, std::uint16_t v)
{
  out += static_cast<char>(v & 0xFF);
  out += static_cast<char>((v >> 8) & 0xFF);
}

void pad4(std::string& out, char fill)
{
  while (out.size() % 4 != 0) {
    out += fill;
  }
}

struct NodeData {
  int mesh = 0;
  bool has_translation = false;
  std::array<double, 3> translation_mm{0, 0, 0};
  int copy_no = 0;
  std::string role;
  std::string name;
  bool sensitive = false;
};

bool is_sensitive(const json& spec, const std::string& volume)
{
  for (const json& s : spec.at("sensitive")) {
    if (s == volume) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string build_geometry_glb(const json& spec)
{
  test_export_failure();
  std::vector<MeshData> meshes;
  std::vector<NodeData> nodes;
  json materials = json::array();
  const auto material = [](const char* name, std::array<double, 4> colour) {
    return json{{"name", name},
                {"alphaMode", "BLEND"},
                {"doubleSided", true},
                {"pbrMetallicRoughness",
                 {{"baseColorFactor", colour}, {"metallicFactor", 0}, {"roughnessFactor", 0.9}}}};
  };
  const bool tracker = spec.at("template") == kTemplateTrackerBarrel;

  if (tracker) {
    materials.push_back(material("tracker", {0.2, 0.7, 0.4, 0.35}));
    const json& layers = spec.at("geometry").at("layers");
    for (std::size_t i = 0; i < layers.size(); ++i) {
      TrackerLayer l;
      l.radius_mm = layers[i].at("radius_mm").get<double>();
      l.thickness_mm = layers[i].at("thickness_mm").get<double>();
      l.half_length_mm = layers[i].at("half_length_mm").get<double>();
      meshes.push_back(make_tube("tracker_layer_" + std::to_string(i), 0, l.inner_radius_mm(), l.outer_radius_mm(),
                                 l.half_length_mm));
      NodeData n;
      n.mesh = static_cast<int>(i);
      n.copy_no = static_cast<int>(i);
      n.role = "tracker";
      n.name = kTrackerVolumeName;
      n.sensitive = is_sensitive(spec, n.name);
      nodes.push_back(n);
    }
  }
  else {
    materials.push_back(material("absorber", {0.6, 0.6, 0.65, 0.35}));
    materials.push_back(material("active", {0.2, 0.5, 0.8, 0.35}));
    const json& g = spec.at("geometry");
    CalorimeterGeometry c;
    c.layers = g.at("layers").get<int>();
    c.absorber_thickness_mm = g.at("absorber").at("thickness_mm").get<double>();
    c.active_thickness_mm = g.at("active").at("thickness_mm").get<double>();
    c.transverse_mm = g.at("transverse_mm").get<double>();
    const double half_xy = 0.5 * c.transverse_mm;
    meshes.push_back(make_box("absorber", 0, half_xy, half_xy, 0.5 * c.absorber_thickness_mm));
    meshes.push_back(make_box("active", 1, half_xy, half_xy, 0.5 * c.active_thickness_mm));
    for (int i = 0; i < c.layers; ++i) {
      const double front = c.layer_front_z_mm(i);
      NodeData a;
      a.mesh = 0;
      a.has_translation = true;
      a.translation_mm = {0, 0, front + 0.5 * c.absorber_thickness_mm};
      a.copy_no = i;
      a.role = "absorber";
      a.name = "absorber";
      a.sensitive = is_sensitive(spec, a.name);
      nodes.push_back(a);
      NodeData b;
      b.mesh = 1;
      b.has_translation = true;
      b.translation_mm = {0, 0, front + c.absorber_thickness_mm + 0.5 * c.active_thickness_mm};
      b.copy_no = i;
      b.role = "active";
      b.name = "active";
      b.sensitive = is_sensitive(spec, b.name);
      nodes.push_back(b);
    }
  }

  // BIN chunk and the accessors that describe it
  std::string bin;
  json buffer_views = json::array();
  json accessors = json::array();
  json mesh_json = json::array();
  for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
    const MeshData& m = meshes[mi];
    const std::size_t vertices = m.positions.size() / 3;
    std::array<float, 3> lo = {m.positions[0], m.positions[1], m.positions[2]};
    std::array<float, 3> hi = lo;
    for (std::size_t v = 0; v < vertices; ++v) {
      for (int axis = 0; axis < 3; ++axis) {
        lo[axis] = std::min(lo[axis], m.positions[3 * v + axis]);
        hi[axis] = std::max(hi[axis], m.positions[3 * v + axis]);
      }
    }
    auto add_view = [&](std::size_t offset, std::size_t length, int target) {
      buffer_views.push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}, {"target", target}});
    };
    std::size_t start = bin.size();
    for (float f : m.positions) put_float(bin, f);
    add_view(start, bin.size() - start, 34962);
    accessors.push_back({{"bufferView", buffer_views.size() - 1},
                         {"componentType", 5126},
                         {"count", vertices},
                         {"type", "VEC3"},
                         {"min", {float_json(lo[0]), float_json(lo[1]), float_json(lo[2])}},
                         {"max", {float_json(hi[0]), float_json(hi[1]), float_json(hi[2])}}});
    start = bin.size();
    for (float f : m.normals) put_float(bin, f);
    add_view(start, bin.size() - start, 34962);
    accessors.push_back({{"bufferView", buffer_views.size() - 1},
                         {"componentType", 5126},
                         {"count", vertices},
                         {"type", "VEC3"}});
    start = bin.size();
    for (std::uint16_t i : m.indices) put_u16(bin, i);
    const std::size_t index_bytes = bin.size() - start;
    pad4(bin, '\0');
    add_view(start, index_bytes, 34963);
    accessors.push_back({{"bufferView", buffer_views.size() - 1},
                         {"componentType", 5123},
                         {"count", m.indices.size()},
                         {"type", "SCALAR"}});
    const std::size_t a = 3 * mi;
    mesh_json.push_back({{"name", m.name},
                         {"primitives",
                          json::array({{{"attributes", {{"POSITION", a}, {"NORMAL", a + 1}}},
                                        {"indices", a + 2},
                                        {"material", m.material},
                                        {"mode", 4}}})}});
  }

  json node_json = json::array();
  json root = json::array();
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const NodeData& n = nodes[i];
    json entry = {{"name", n.name},
                  {"mesh", n.mesh},
                  {"extras", {{"copy_no", n.copy_no}, {"role", n.role}, {"sensitive", n.sensitive}}}};
    if (n.has_translation) {
      entry["translation"] = {float_json(static_cast<float>(n.translation_mm[0] * kMetresPerMm)),
                              float_json(static_cast<float>(n.translation_mm[1] * kMetresPerMm)),
                              float_json(static_cast<float>(n.translation_mm[2] * kMetresPerMm))};
    }
    node_json.push_back(entry);
    root.push_back(i);
  }

  json asset_extras = {{"export_format_version", kExportFormatVersion},
                       {"length_unit", "m"},
                       {"mm_per_unit", 1000},
                       {"frame", "geant4_global"},
                       {"beam_axis", "+z"},
                       {"axes",
                        "x, y, z of the Geant4 global frame; no axis swapped (glTF's conventional up is +y; the file "
                        "does not remap it)"},
                       {"template", spec.at("template")},
                       {"spec_oid", spec_oid_of(spec)}};
  if (tracker) {
    asset_extras["tube_segments"] = kTubeSegments;
  }
  json doc = {{"asset", {{"generator", "vikshep-sim"}, {"version", "2.0"}, {"extras", asset_extras}}},
              {"scene", 0},
              {"scenes", json::array({{{"nodes", root}}})},
              {"nodes", node_json},
              {"meshes", mesh_json},
              {"materials", materials},
              {"accessors", accessors},
              {"bufferViews", buffer_views},
              {"buffers", json::array({{{"byteLength", bin.size()}}})}};
  std::string text = doc.dump();
  pad4(text, ' ');
  pad4(bin, '\0');

  std::string out;
  put_u32(out, 0x46546C67);  // "glTF"
  put_u32(out, 2);
  put_u32(out, static_cast<std::uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
  put_u32(out, static_cast<std::uint32_t>(text.size()));
  put_u32(out, 0x4E4F534A);  // "JSON"
  out += text;
  put_u32(out, static_cast<std::uint32_t>(bin.size()));
  put_u32(out, 0x004E4942);  // "BIN\0"
  out += bin;
  return out;
}

std::string spec_oid_of(const json& resolved_spec)
{
  return oid_of_bytes(canonical_spec(resolved_spec));
}

ExportedFile write_geometry_glb(const std::string& path, const json& spec)
{
  const std::string bytes = build_geometry_glb(spec);
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      throw ExportError("cannot write " + std::string(kGeometryFileName));
    }
  }
  return describe_file(path, base_name(path));
}

json exported_file_json(const ExportedFile& file)
{
  return {{"bytes", file.bytes}, {"file", file.name}, {"oid", file.oid}};
}
