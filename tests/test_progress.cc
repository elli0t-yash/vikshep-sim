#include <chrono>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "progress.hh"

using namespace std::chrono_literals;

namespace {

// A clock that the test moves by hand.
struct FakeClock {
  std::chrono::steady_clock::time_point now{};
  ProgressThrottle::Clock fn() { return [this] { return now; }; }
};

}  // namespace

TEST_CASE("the first line is always written and the next ones are throttled", "[progress]")
{
  FakeClock clock;
  ProgressThrottle throttle(1000, 250ms, clock.fn());
  CHECK(throttle.should_emit(0));            // first line, done = 0
  clock.now += 10ms;
  CHECK_FALSE(throttle.should_emit(1));
  clock.now += 200ms;                         // 210 ms after the first line
  CHECK_FALSE(throttle.should_emit(50));
  clock.now += 40ms;                          // 250 ms after the first line
  CHECK(throttle.should_emit(60));
  clock.now += 249ms;
  CHECK_FALSE(throttle.should_emit(300));
  clock.now += 1ms;
  CHECK(throttle.should_emit(310));
}

TEST_CASE("the last line is always written, once", "[progress]")
{
  FakeClock clock;
  ProgressThrottle throttle(10, 250ms, clock.fn());
  CHECK(throttle.should_emit(0));
  clock.now += 1ms;
  CHECK_FALSE(throttle.should_emit(9));
  clock.now += 1ms;
  CHECK(throttle.should_emit(10));            // done = total, 2 ms after the previous line
  CHECK_FALSE(throttle.should_emit(10));      // not again
  clock.now += 10s;
  CHECK_FALSE(throttle.should_emit(10));
}

TEST_CASE("a run of one event gives the first and the last line", "[progress]")
{
  FakeClock clock;
  ProgressThrottle throttle(1, 250ms, clock.fn());
  CHECK(throttle.should_emit(0));
  CHECK(throttle.should_emit(1));
  CHECK_FALSE(throttle.should_emit(1));
}

TEST_CASE("the lines written never come closer than the interval, except the last", "[progress]")
{
  FakeClock clock;
  ProgressThrottle throttle(100000, 250ms, clock.fn());
  std::vector<std::chrono::steady_clock::time_point> written;
  for (std::uint64_t done = 0; done < 100000; done += 7) {
    clock.now += 3ms;  // events finish every 3 ms
    if (throttle.should_emit(done)) {
      written.push_back(clock.now);
    }
  }
  REQUIRE(written.size() > 5);
  for (std::size_t i = 1; i < written.size(); ++i) {
    CHECK(written[i] - written[i - 1] >= 250ms);
  }
}
