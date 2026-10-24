#include "spec_canonical.hh"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Canonical form: RFC 8785 (JSON Canonicalization Scheme) as far as the
// documented spec needs it. Differences from RFC 8785:
//  - Object keys are sorted by their UTF-8 bytes (std::map order), RFC 8785
//    sorts by UTF-16 code units. They differ only for non-BMP characters, and a
//    valid spec contains only the ASCII keys of docs/SPEC.md.
//  - Strings are written by nlohmann::json::dump(). It uses the JSON short
//    escapes and lowercase \u00xx for other control characters, which is what
//    RFC 8785 requires, but it throws on invalid UTF-8 (a valid spec has none).
//  - Only finite numbers are supported (JSON has no others).
// Numbers follow the ECMAScript Number-to-String algorithm, as RFC 8785 asks.

using nlohmann::json;

namespace {

// ECMAScript Number::toString(10) for a finite double.
std::string format_number(double value)
{
  if (!std::isfinite(value)) {
    throw std::invalid_argument("canonical_spec: non-finite number");
  }
  if (value == 0.0) {
    return "0";  // also for -0
  }
  std::string out;
  if (value < 0.0) {
    out = "-";
    value = -value;
  }

  // Shortest digits that read back to the same double: "d.ddde+XX".
  char buf[64];
  const auto res = std::to_chars(buf, buf + sizeof buf, value, std::chars_format::scientific);
  const std::string sci(buf, res.ptr);
  const std::size_t epos = sci.find('e');
  std::string digits;
  for (std::size_t i = 0; i < epos; ++i) {
    if (sci[i] != '.') {
      digits += sci[i];
    }
  }
  const int exponent = std::stoi(sci.substr(epos + 1));
  const int k = static_cast<int>(digits.size());
  const int n = exponent + 1;  // value = 0.digits * 10^n

  if (k <= n && n <= 21) {
    out += digits + std::string(static_cast<std::size_t>(n - k), '0');
  }
  else if (0 < n && n <= 21) {
    out += digits.substr(0, static_cast<std::size_t>(n)) + "." +
           digits.substr(static_cast<std::size_t>(n));
  }
  else if (-6 < n && n <= 0) {
    out += "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
  }
  else {
    const int e = n - 1;
    out += digits.substr(0, 1);
    if (k > 1) {
      out += "." + digits.substr(1);
    }
    out += (e < 0 ? "e-" : "e+") + std::to_string(e < 0 ? -e : e);
  }
  return out;
}

void write(const json& j, std::string& out)
{
  switch (j.type()) {
    case json::value_t::object: {
      out += '{';
      bool first = true;
      for (auto it = j.begin(); it != j.end(); ++it) {  // std::map: sorted
        if (!first) {
          out += ',';
        }
        first = false;
        out += json(it.key()).dump();
        out += ':';
        write(it.value(), out);
      }
      out += '}';
      break;
    }
    case json::value_t::array: {
      out += '[';
      for (std::size_t i = 0; i < j.size(); ++i) {
        if (i != 0) {
          out += ',';
        }
        write(j[i], out);
      }
      out += ']';
      break;
    }
    case json::value_t::string:
      out += j.dump();
      break;
    case json::value_t::boolean:
      out += j.get<bool>() ? "true" : "false";
      break;
    case json::value_t::null:
      out += "null";
      break;
    case json::value_t::number_integer:
      out += std::to_string(j.get<std::int64_t>());
      break;
    case json::value_t::number_unsigned:
      out += std::to_string(j.get<std::uint64_t>());
      break;
    case json::value_t::number_float:
      out += format_number(j.get<double>());
      break;
    default:
      throw std::invalid_argument("canonical_spec: unsupported JSON value");
  }
}

// An integer-valued number as an integer (42 and 42.0 become 42).
json as_integer(const json& j)
{
  if (j.is_number_float()) {
    return static_cast<std::int64_t>(j.get<double>());
  }
  if (j.is_number_unsigned()) {
    return j.get<std::uint64_t>();
  }
  return j.get<std::int64_t>();
}

json as_double(const json& j)
{
  return j.get<double>();
}

json as_vector3(const json& j)
{
  return json::array({j[0].get<double>(), j[1].get<double>(), j[2].get<double>()});
}

json as_normalised_vector3(const json& j)
{
  const double x = j[0].get<double>();
  const double y = j[1].get<double>();
  const double z = j[2].get<double>();
  const double norm = std::hypot(std::hypot(x, y), z);
  return json::array({x / norm, y / norm, z / norm});
}

json resolve_slab(const json& slab)
{
  return {{"material", slab.at("material")}, {"thickness_mm", as_double(slab.at("thickness_mm"))}};
}

}  // namespace

json resolve_spec(const json& spec)
{
  const json& geometry = spec.at("geometry");
  const json& gun = spec.at("gun");

  json out;
  out["spec_version"] = spec.at("spec_version");
  out["template"] = spec.at("template");
  out["geometry"] = {
      {"layers", as_integer(geometry.at("layers"))},
      {"absorber", resolve_slab(geometry.at("absorber"))},
      {"active", resolve_slab(geometry.at("active"))},
      {"transverse_mm", as_double(geometry.at("transverse_mm"))},
  };
  out["gun"] = {
      {"particle", gun.at("particle")},
      {"energy_GeV", as_double(gun.at("energy_GeV"))},
      {"position_mm", gun.contains("position_mm") ? as_vector3(gun.at("position_mm"))
                                                  : json::array({0.0, 0.0, -20.0})},
      {"direction", gun.contains("direction") ? as_normalised_vector3(gun.at("direction"))
                                              : json::array({0.0, 0.0, 1.0})},
  };
  out["physics"] = {{"list", spec.contains("physics") && spec.at("physics").contains("list")
                                 ? spec.at("physics").at("list")
                                 : json("FTFP_BERT")}};

  std::vector<std::string> sensitive = {"active"};
  if (spec.contains("sensitive")) {
    sensitive = spec.at("sensitive").get<std::vector<std::string>>();
  }
  std::sort(sensitive.begin(), sensitive.end());
  out["sensitive"] = sensitive;

  out["events"] = as_integer(spec.at("events"));
  out["seed"] = as_integer(spec.at("seed"));
  return out;
}

std::string canonical_spec(const json& spec)
{
  std::string out;
  write(resolve_spec(spec), out);
  return out;
}
