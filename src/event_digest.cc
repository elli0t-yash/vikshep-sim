#include "event_digest.hh"

#include <cmath>
#include <cstdio>

namespace {

constexpr std::uint64_t kFnvOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001B3ULL;

class Fnv {
 public:
  std::uint64_t value() const { return hash_; }

  void add32(std::int32_t v)
  {
    const std::uint32_t u = static_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; ++i) {
      add_byte(static_cast<std::uint8_t>(u >> (8 * i)));
    }
  }

  void add64(std::int64_t v)
  {
    const std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
      add_byte(static_cast<std::uint8_t>(u >> (8 * i)));
    }
  }

  void add_quantised(double value, double quantum)
  {
    add64(static_cast<std::int64_t>(std::llround(value / quantum)));
  }

 private:
  void add_byte(std::uint8_t b)
  {
    hash_ ^= b;
    hash_ *= kFnvPrime;
  }
  std::uint64_t hash_ = kFnvOffset;
};

std::string hex16(std::uint64_t v)
{
  char buffer[17];
  std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(v));
  return buffer;
}

}  // namespace

EventDigest make_digest(const EventHits& event)
{
  EventDigest d;
  d.event_id = event.event_id;
  d.hits = event.hits.size();
  Fnv fnv;
  for (const Hit& h : event.hits) {
    d.edep_sum_mev += h.edep_mev;
    d.hits_by_volume[static_cast<std::size_t>(h.volume)] += 1;
    d.edep_by_volume_mev[static_cast<std::size_t>(h.volume)] += h.edep_mev;

    fnv.add32(h.volume);
    fnv.add32(h.copy_no);
    fnv.add32(h.layer);
    fnv.add_quantised(h.x_mm, 1e-6);
    fnv.add_quantised(h.y_mm, 1e-6);
    fnv.add_quantised(h.z_mm, 1e-6);
    fnv.add_quantised(h.t_ns, 1e-6);
    fnv.add_quantised(h.edep_mev, 1e-9);
    fnv.add_quantised(h.px_mev, 1e-6);
    fnv.add_quantised(h.py_mev, 1e-6);
    fnv.add_quantised(h.pz_mev, 1e-6);
    fnv.add32(h.track_id);
    fnv.add32(h.parent_id);
    fnv.add32(h.pdg);
    fnv.add32(h.step);
  }
  d.hash = fnv.value();
  return d;
}

nlohmann::json digest_to_json(const EventDigest& d, const std::vector<std::string>& volumes)
{
  nlohmann::json counts = nlohmann::json::object();
  nlohmann::json energies = nlohmann::json::object();
  for (const std::string& name : volumes) {
    for (std::size_t i = 0; i < 2; ++i) {
      if (name == kSlabNames[i]) {
        counts[name] = d.hits_by_volume[i];
        energies[name] = d.edep_by_volume_mev[i];
      }
    }
  }
  return {
      {"edep_by_volume_MeV", energies},
      {"edep_sum_MeV", d.edep_sum_mev},
      {"event_id", d.event_id},
      {"hash", hex16(d.hash)},
      {"hits", d.hits},
      {"hits_by_volume", counts},
  };
}
