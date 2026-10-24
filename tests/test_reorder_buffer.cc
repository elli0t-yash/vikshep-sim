#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "reorder_buffer.hh"

using namespace std::chrono_literals;

namespace {

// Records what the consumer saw and checks it is never called concurrently.
struct Recorder {
  std::vector<std::uint64_t> ids;
  std::vector<std::string> payloads;
  std::atomic<int> inside{0};
  std::atomic<int> max_inside{0};

  void operator()(std::uint64_t id, std::string&& payload)
  {
    const int now = ++inside;
    int seen = max_inside.load();
    while (now > seen && !max_inside.compare_exchange_weak(seen, now)) {
    }
    ids.push_back(id);  // safe only because calls are serialised, which is checked
    payloads.push_back(std::move(payload));
    --inside;
  }
};

std::vector<std::uint64_t> iota_ids(std::uint64_t n)
{
  std::vector<std::uint64_t> v(n);
  std::iota(v.begin(), v.end(), 0);
  return v;
}

}  // namespace

TEST_CASE("in-order submissions are released at once", "[reorder]")
{
  Recorder r;
  ReorderBuffer<std::string> buffer([&](std::uint64_t id, std::string&& p) { r(id, std::move(p)); });
  for (std::uint64_t i = 0; i < 5; ++i) {
    buffer.submit(i, "e" + std::to_string(i));
    CHECK(buffer.pending() == 0);
    CHECK(buffer.next_id() == i + 1);
  }
  CHECK(r.ids == iota_ids(5));
  CHECK(r.payloads[3] == "e3");
}

TEST_CASE("scrambled submissions come out in order", "[reorder]")
{
  std::mt19937 rng(12345);
  for (int round = 0; round < 20; ++round) {
    Recorder r;
    ReorderBuffer<std::string> buffer(
        [&](std::uint64_t id, std::string&& p) { r(id, std::move(p)); });
    std::vector<std::uint64_t> order = iota_ids(60);
    std::shuffle(order.begin(), order.end(), rng);
    for (std::uint64_t id : order) {
      buffer.submit(id, std::to_string(id));
    }
    REQUIRE(r.ids == iota_ids(60));
    for (std::size_t i = 0; i < r.ids.size(); ++i) {
      REQUIRE(r.payloads[i] == std::to_string(i));
    }
    CHECK(buffer.pending() == 0);
  }
}

TEST_CASE("the first id can be set", "[reorder]")
{
  Recorder r;
  ReorderBuffer<std::string> buffer([&](std::uint64_t id, std::string&& p) { r(id, std::move(p)); },
                                    10);
  buffer.submit(11, "b");
  CHECK(r.ids.empty());
  buffer.submit(10, "a");
  CHECK(r.ids == (std::vector<std::uint64_t>{10, 11}));
}

TEST_CASE("interleaved submissions from many threads", "[reorder]")
{
  constexpr std::uint64_t kEvents = 2000;
  constexpr int kThreads = 8;
  Recorder r;
  ReorderBuffer<std::string> buffer([&](std::uint64_t id, std::string&& p) { r(id, std::move(p)); });

  std::atomic<std::uint64_t> next_to_take{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      std::mt19937 rng(1000 + t);
      for (;;) {
        const std::uint64_t id = next_to_take++;
        if (id >= kEvents) {
          return;
        }
        // Uneven "work" so that completion order differs from start order.
        if (rng() % 7 == 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(rng() % 2000));
        }
        buffer.submit(id, std::to_string(id));
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  REQUIRE(r.ids == iota_ids(kEvents));
  CHECK(r.max_inside.load() == 1);
  CHECK(buffer.pending() == 0);
  CHECK(buffer.next_id() == kEvents);
}

TEST_CASE("a slow first event holds everything back, then releases it in order", "[reorder]")
{
  Recorder r;
  ReorderBuffer<std::string> buffer([&](std::uint64_t id, std::string&& p) { r(id, std::move(p)); });

  std::promise<void> first_may_finish;
  std::future<void> gate = first_may_finish.get_future();
  std::thread slow([&] {
    gate.wait();  // event 0 is "still computing"
    buffer.submit(0, "slow");
  });

  std::vector<std::thread> fast;
  for (int t = 0; t < 4; ++t) {
    fast.emplace_back([&, t] {
      for (std::uint64_t id = 1 + t; id < 100; id += 4) {
        buffer.submit(id, std::to_string(id));
      }
    });
  }
  for (auto& th : fast) {
    th.join();
  }
  // All 99 later events are in, none released.
  CHECK(buffer.pending() == 99);
  CHECK(buffer.high_water_mark() == 99);
  CHECK(buffer.next_id() == 0);

  first_may_finish.set_value();
  slow.join();
  REQUIRE(r.ids == iota_ids(100));
  CHECK(r.payloads[0] == "slow");
  CHECK(buffer.pending() == 0);
}

TEST_CASE("bounded buffer: a slow first event does not deadlock and memory stays bounded",
          "[reorder]")
{
  constexpr std::uint64_t kEvents = 400;
  constexpr std::size_t kAhead = 6;
  Recorder r;
  ReorderBuffer<std::string> buffer([&](std::uint64_t id, std::string&& p) { r(id, std::move(p)); },
                                    0, kAhead);

  auto run = std::async(std::launch::async, [&] {
    // Workers take ids in increasing order, as Geant4's run manager does.
    std::atomic<std::uint64_t> next_to_take{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
      threads.emplace_back([&] {
        for (;;) {
          const std::uint64_t id = next_to_take++;
          if (id >= kEvents) {
            return;
          }
          if (id == 0) {
            std::this_thread::sleep_for(200ms);  // the slow first event
          }
          buffer.submit(id, std::to_string(id));
        }
      });
    }
    for (auto& th : threads) {
      th.join();
    }
  });
  REQUIRE(run.wait_for(30s) == std::future_status::ready);  // no deadlock
  run.get();
  REQUIRE(r.ids == iota_ids(kEvents));
  CHECK(buffer.high_water_mark() <= kAhead);
}

TEST_CASE("bounded buffer: a submitter too far ahead waits until the window moves", "[reorder]")
{
  Recorder r;
  ReorderBuffer<std::string> buffer([&](std::uint64_t id, std::string&& p) { r(id, std::move(p)); },
                                    0, 2);
  std::atomic<bool> far_done{false};
  std::thread far([&] {
    buffer.submit(2, "two");  // 2 >= 0 + 2: must wait
    far_done = true;
  });
  std::this_thread::sleep_for(100ms);
  CHECK_FALSE(far_done.load());
  buffer.submit(1, "one");  // inside the window, held
  buffer.submit(0, "zero"); // releases 0 and 1, which opens the window for 2
  far.join();
  CHECK(far_done.load());
  CHECK(r.ids == iota_ids(3));
}

TEST_CASE("a repeated or already released id is an error", "[reorder]")
{
  ReorderBuffer<int> buffer([](std::uint64_t, int&&) {});
  buffer.submit(0, 1);
  CHECK_THROWS_AS(buffer.submit(0, 2), std::logic_error);  // already released
  buffer.submit(3, 4);
  CHECK_THROWS_AS(buffer.submit(3, 5), std::logic_error);  // already pending
}

TEST_CASE("a consumer that throws fails the buffer without leaving anyone waiting", "[reorder]")
{
  ReorderBuffer<int> buffer(
      [](std::uint64_t id, int&&) {
        if (id == 2) {
          throw std::runtime_error("disk full");
        }
      },
      0, 3);
  buffer.submit(0, 0);
  buffer.submit(1, 1);
  CHECK_THROWS_WITH(buffer.submit(2, 2), Catch::Matchers::Equals("disk full"));
  CHECK_THROWS_AS(buffer.submit(3, 3), std::runtime_error);

  // A submitter blocked on the window is woken up and returns (or throws): it
  // is never left waiting. Either outcome is fine; hanging is not.
  ReorderBuffer<int> b2([](std::uint64_t, int&&) { throw std::runtime_error("boom"); }, 0, 1);
  auto waiter = std::async(std::launch::async, [&] {
    try {
      b2.submit(1, 1);  // 1 >= 0 + 1: waits
    }
    catch (const std::runtime_error&) {
    }
  });
  std::this_thread::sleep_for(100ms);
  CHECK_THROWS_WITH(b2.submit(0, 0), Catch::Matchers::Equals("boom"));
  CHECK(waiter.wait_for(10s) == std::future_status::ready);
  waiter.get();
}
