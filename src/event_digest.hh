#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "hits.hh"

// EXPERIMENTAL. A short fingerprint of one event's hits, for tests and for
// comparing runs (`dev-run --event-digests`). Outside the sidecar contract.
//
// hash: FNV-1a, 64 bit, over the rows in step order. Each row contributes, in
// the column order of docs/HITS.md, these integers, each as little-endian
// bytes:
//   volume, copy_no, layer                         32 bits each
//   x, y, z                                        64 bits, mm  quantised to 1e-6 mm
//   t                                              64 bits, ns  quantised to 1e-6 ns
//   edep                                           64 bits, MeV quantised to 1e-9 MeV
//   px, py, pz                                     64 bits, MeV/c quantised to 1e-6 MeV/c
//   track_id, parent_id, pdg, step                 32 bits each
// A quantised value is llround(value / quantum). The quantisation makes the
// hash insensitive to last-bit differences, which is why it is not a
// byte-for-byte fingerprint (the OID of hits.arrow is that). The event id is
// not hashed; it is a separate field. An event with no hits hashes to the FNV
// offset basis.
struct EventDigest {
  std::uint64_t event_id = 0;
  std::uint64_t hits = 0;
  double edep_sum_mev = 0.0;
  std::uint64_t hash = 0;
  std::array<std::uint64_t, kVolumeCount> hits_by_volume{};
  std::array<double, kVolumeCount> edep_by_volume_mev{};
};

EventDigest make_digest(const EventHits& event);

// One JSON object with sorted keys. hits_by_volume and edep_by_volume_MeV have
// an entry for each name in volumes (the sensitive volumes of the spec).
nlohmann::json digest_to_json(const EventDigest& digest, const std::vector<std::string>& volumes);
