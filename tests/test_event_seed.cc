#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "Randomize.hh"
#include "event_seed.hh"

TEST_CASE("event seeds match the definition in HITS.md", "[seed]")
{
  // Computed with an independent implementation of the definition (Python).
  struct Golden {
    std::uint64_t seed, event, first, second;
  };
  const std::vector<Golden> golden = {
      {0, 0, 0x1B6BF2BFE4FA70F3ULL, 0x53B8EC1FULL},
      {42, 0, 0x23EEB6C6D49A9E51ULL, 0x7DF8CCAFULL},
      {42, 1, 0x3426247AA9C440AAULL, 0x020304E7ULL},
      {42, 19, 0x7EAC6D7BC47EB242ULL, 0xE0D6B94AULL},
      {43, 0, 0x3A48103D16C39210ULL, 0x1CFC01EAULL},
      {9007199254740991, 10000000, 0x134DCB16B6F86504ULL, 0x895C30DEULL},
  };
  for (const Golden& g : golden) {
    const EventSeeds s = event_seeds(g.seed, g.event);
    CHECK(s.first == g.first);
    CHECK(s.second == g.second);
  }
}

TEST_CASE("event seeds fit the bits the engine uses", "[seed]")
{
  for (std::uint64_t e = 0; e < 1000; ++e) {
    const EventSeeds s = event_seeds(7, e);
    CHECK(s.first < (1ULL << 63));   // 63 bits
    CHECK(s.second < (1ULL << 32));  // 32 bits
  }
}

TEST_CASE("event seeds are distinct across seeds and events", "[seed]")
{
  std::set<std::pair<std::uint64_t, std::uint64_t>> seen;
  for (std::uint64_t seed = 0; seed < 50; ++seed) {
    for (std::uint64_t e = 0; e < 200; ++e) {
      const EventSeeds s = event_seeds(seed, e);
      seen.insert({s.first, s.second});
    }
  }
  CHECK(seen.size() == 50u * 200u);
  // The first seed does not repeat either, so events are not paired by one value.
  std::set<std::uint64_t> firsts;
  for (std::uint64_t e = 0; e < 10000; ++e) {
    firsts.insert(event_seeds(42, e).first);
  }
  CHECK(firsts.size() == 10000u);
}

TEST_CASE("the seeds depend on nothing else", "[seed]")
{
  // Same inputs, same outputs, however often and from whichever thread.
  const EventSeeds a = event_seeds(42, 5);
  EventSeeds b{0, 0};
  std::thread t([&] { b = event_seeds(42, 5); });
  t.join();
  CHECK(a.first == b.first);
  CHECK(a.second == b.second);
}

namespace {

std::string engine_state()
{
  std::ostringstream os;
  CLHEP::HepRandom::getTheEngine()->put(os);
  return os.str();
}

}  // namespace

TEST_CASE("reseeding the engine gives the same stream and uses both seeds", "[seed]")
{
  seed_event_engine(42, 3);
  const double a1 = CLHEP::HepRandom::getTheEngine()->flat();
  const double a2 = CLHEP::HepRandom::getTheEngine()->flat();

  // Draw some other numbers, then reseed: the stream restarts exactly.
  for (int i = 0; i < 1000; ++i) {
    CLHEP::HepRandom::getTheEngine()->flat();
  }
  seed_event_engine(42, 3);
  CHECK(CLHEP::HepRandom::getTheEngine()->flat() == a1);
  CHECK(CLHEP::HepRandom::getTheEngine()->flat() == a2);

  // Another event, another stream.
  seed_event_engine(42, 4);
  CHECK(CLHEP::HepRandom::getTheEngine()->flat() != a1);

  // The engine state depends on every bit of the first seed and on the low 32
  // bits of the second. (The upper 32 bits of the second seed are ignored by
  // this CLHEP MixMax, which is why event_seeds() makes it a 32-bit value.)
  const long base[2] = {0x1234567890ABCDEL, 0x0FEDCBA987654321L};
  CLHEP::HepRandom::setTheSeeds(base, 2);
  const std::string reference = engine_state();
  for (int which = 0; which < 2; ++which) {
    for (int bit : which == 0 ? std::vector<int>{0, 31, 32, 62} : std::vector<int>{0, 15, 31}) {
      long changed[2] = {base[0], base[1]};
      changed[which] ^= (1L << bit);
      CLHEP::HepRandom::setTheSeeds(changed, 2);
      INFO("seed " << which << " bit " << bit);
      CHECK(engine_state() != reference);
    }
  }
}
