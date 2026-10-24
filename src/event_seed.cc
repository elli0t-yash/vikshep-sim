
#include <cstdint>
#include "event_seed.hh"

#include "Randomize.hh"

namespace {

std::uint64_t mix(std::uint64_t z)
{
  z ^= z >> 30;
  z *= 0xBF58476D1CE4E5B9ULL;
  z ^= z >> 27;
  z *= 0x94D049BB133111EBULL;
  z ^= z >> 31;
  return z;
}

constexpr std::uint64_t kGamma = 0x9E3779B97F4A7C15ULL;

}  // namespace

EventSeeds event_seeds(std::uint64_t spec_seed, std::uint64_t event_id)
{
  const std::uint64_t x0 = mix(spec_seed ^ 0x6A09E667F3BCC909ULL) + mix(event_id ^ 0xBB67AE8584CAA73BULL);
  const std::uint64_t x1 = x0 + kGamma;
  const std::uint64_t x2 = x1 + kGamma;
  return {mix(x1) >> 1, mix(x2) >> 32};
}

void seed_event_engine(std::uint64_t spec_seed, std::uint64_t event_id)
{
  const EventSeeds s = event_seeds(spec_seed, event_id);
  const long seeds[2] = {static_cast<long>(s.first), static_cast<long>(s.second)};
  CLHEP::HepRandom::setTheSeeds(seeds, 2);
}
