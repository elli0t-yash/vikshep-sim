#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

// Decides when a progress line is written (docs/SIDECAR.md, section 2.2):
//  - the first line, done = 0, is written when the run starts;
//  - after that, at most one line every interval (250 ms by default);
//  - the line with done = total is always written, and written once.
// The throttle only decides; the caller writes the line. Not thread-safe by itself: the
// pipeline calls it from one place at a time (the reorder buffer's consumer).
class ProgressThrottle {
 public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  explicit ProgressThrottle(std::uint64_t total,
                            std::chrono::milliseconds interval = std::chrono::milliseconds(250),
                            Clock clock = [] { return std::chrono::steady_clock::now(); })
      : total_(total), interval_(interval), clock_(std::move(clock))
  {
  }

  // True if a line with this value of `done` should be written now.
  bool should_emit(std::uint64_t done)
  {
    if (finished_) {
      return false;
    }
    const auto now = clock_();
    const bool last = done >= total_;
    if (!started_ || last || now - last_emit_ >= interval_) {
      started_ = true;
      last_emit_ = now;
      finished_ = last;
      return true;
    }
    return false;
  }

 private:
  std::uint64_t total_;
  std::chrono::milliseconds interval_;
  Clock clock_;
  bool started_ = false;
  bool finished_ = false;
  std::chrono::steady_clock::time_point last_emit_;
};
