#pragma once

#include <cstdint>

// Per-event random number seeds (docs/HITS.md, section 4.2).
//
// The random stream of an event depends only on (spec seed, event_id): not on
// the number of threads, which thread ran the event, how many events the run
// has, or which events ran before it. Every event reseeds its thread's random
// engine from the two values below at the start of the event.
//
// Definition (all arithmetic modulo 2^64):
//   mix(z):  z ^= z >> 30;  z *= 0xBF58476D1CE4E5B9;
//            z ^= z >> 27;  z *= 0x94D049BB133111EB;  z ^= z >> 31;  return z
//            (the SplitMix64 finalizer)
//   x0   = mix(seed ^ 0x6A09E667F3BCC909) + mix(event_id ^ 0xBB67AE8584CAA73B)
//   x1   = x0 + 0x9E3779B97F4A7C15;   a = mix(x1)
//   x2   = x1 + 0x9E3779B97F4A7C15;   b = mix(x2)
//   seeds = { a >> 1, b >> 32 }
//           the first seed is the top 63 bits of a (a non-negative signed
//           64-bit long), the second the top 32 bits of b. CLHEP's MixMax engine
//           (Geant4's default) builds its stream from the first seed and the low
//           32 bits of the second; the upper 32 bits of the second are ignored
//           (measured, tests/test_event_seed.cc), so none are wasted here.
struct EventSeeds {
  std::uint64_t first;
  std::uint64_t second;
};

EventSeeds event_seeds(std::uint64_t spec_seed, std::uint64_t event_id);

// Reseeds the calling thread's CLHEP random engine for an event. Call it before
// anything draws a random number for that event. Assumes the default MixMax
// engine, which Geant4 uses, and a 64-bit long.
void seed_event_engine(std::uint64_t spec_seed, std::uint64_t event_id);
