#include <string>
#include <vector>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "event_digest.hh"

namespace {

Hit make_hit(int volume, int copy)
{
  Hit h;
  h.volume = volume;
  h.copy_no = copy;
  h.layer = copy;
  h.x_mm = 1.5;
  h.y_mm = -2.25;
  h.z_mm = 30.125;
  h.t_ns = 0.0625;
  h.edep_mev = 0.5;
  h.px_mev = 10.0;
  h.py_mev = -1.0;
  h.pz_mev = 100.0;
  h.track_id = 3;
  h.parent_id = 1;
  h.pdg = 11;
  h.step = 4;
  return h;
}

EventHits event(std::uint64_t id, std::vector<Hit> hits)
{
  EventHits e;
  e.event_id = id;
  e.hits = std::move(hits);
  return e;
}

}  // namespace

TEST_CASE("an event without hits has count 0 and the FNV offset basis as hash", "[digest]")
{
  const EventDigest d = make_digest(event(7, {}));
  CHECK(d.event_id == 7);
  CHECK(d.hits == 0);
  CHECK(d.edep_sum_mev == 0.0);
  CHECK(d.hash == 0xCBF29CE484222325ULL);
}

TEST_CASE("counts and energy sums per volume", "[digest]")
{
  Hit a = make_hit(0, 1);
  Hit b = make_hit(1, 2);
  Hit c = make_hit(1, 3);
  b.edep_mev = 0.25;
  c.edep_mev = 0.125;
  const EventDigest d = make_digest(event(0, {a, b, c}));
  CHECK(d.hits == 3);
  CHECK(d.edep_sum_mev == 0.875);
  CHECK(d.hits_by_volume[0] == 1);
  CHECK(d.hits_by_volume[1] == 2);
  CHECK(d.edep_by_volume_mev[0] == 0.5);
  CHECK(d.edep_by_volume_mev[1] == 0.375);
}

TEST_CASE("the hash is deterministic and sensitive to every field and to the order", "[digest]")
{
  const Hit base = make_hit(0, 1);
  const std::uint64_t reference = make_digest(event(0, {base})).hash;
  CHECK(make_digest(event(0, {base})).hash == reference);
  CHECK(make_digest(event(99, {base})).hash == reference);  // the event id is not hashed

  std::vector<Hit> variants(15, base);
  variants[0].volume = 1;
  variants[1].copy_no += 1;
  variants[2].layer += 1;
  variants[3].x_mm += 1e-3;
  variants[4].y_mm += 1e-3;
  variants[5].z_mm += 1e-3;
  variants[6].t_ns += 1e-3;
  variants[7].edep_mev += 1e-3;
  variants[8].px_mev += 1e-3;
  variants[9].py_mev += 1e-3;
  variants[10].pz_mev += 1e-3;
  variants[11].track_id += 1;
  variants[12].parent_id += 1;
  variants[13].pdg += 1;
  variants[14].step += 1;
  for (std::size_t i = 0; i < variants.size(); ++i) {
    INFO("field index " << i);
    CHECK(make_digest(event(0, {variants[i]})).hash != reference);
  }

  const Hit other = make_hit(1, 2);
  CHECK(make_digest(event(0, {base, other})).hash != make_digest(event(0, {other, base})).hash);
  CHECK(make_digest(event(0, {base, base})).hash != reference);
}

TEST_CASE("differences below the quantum do not change the hash", "[digest]")
{
  const Hit base = make_hit(0, 1);
  const std::uint64_t reference = make_digest(event(0, {base})).hash;
  Hit tiny = base;
  tiny.x_mm += 1e-9;    // quantum 1e-6 mm
  tiny.t_ns += 1e-9;    // 1e-6 ns
  tiny.edep_mev += 1e-12;  // 1e-9 MeV
  tiny.px_mev += 1e-9;  // 1e-6 MeV/c
  CHECK(make_digest(event(0, {tiny})).hash == reference);
}

TEST_CASE("digest JSON has sorted keys and one entry per listed volume", "[digest]")
{
  Hit a = make_hit(0, 1);
  const EventDigest d = make_digest(event(5, {a}));
  const nlohmann::json both = digest_to_json(d, {"absorber", "active"});
  CHECK(both.dump() ==
        std::string(R"({"edep_by_volume_MeV":{"absorber":0.5,"active":0.0},"edep_sum_MeV":0.5,)") +
            R"("event_id":5,"hash":")" + both.at("hash").get<std::string>() +
            R"(","hits":1,"hits_by_volume":{"absorber":1,"active":0}})");
  CHECK(both.at("hash").get<std::string>().size() == 16);

  const nlohmann::json only_active = digest_to_json(d, {"active"});
  CHECK(only_active.at("hits_by_volume").size() == 1);
  CHECK(only_active.at("hits_by_volume").contains("active"));
}
