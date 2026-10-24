#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <string>

// Accepts completed events in any order, from any thread, and hands them to a
// consumer strictly in event_id order (first_id, first_id + 1, ...). This is
// what makes the order of the output independent of the number of threads
// (docs/HITS.md, section 4).
//
// Guarantees
//  - The consumer is called for id n only after it returned for id n - 1, and
//    never from two threads at the same time. It runs on whichever submitting
//    thread completes the next id; that thread releases every id that has
//    become ready, so a submitter whose id is not next returns at once.
//  - submit() of an id that was already submitted or already released throws
//    std::logic_error.
//
// Memory
//  - Unbounded by default (max_ahead = 0): submit() never blocks, so it cannot
//    deadlock, but a slow event n holds back every event after it, and all of
//    them stay in memory until n arrives. pending() and high_water_mark()
//    report this.
//  - With max_ahead = k > 0, submit(id) waits while id >= next_id() + k, so at
//    most k events are ever held. The event next_id() itself is never made to
//    wait (next_id() < next_id() + k), so progress is always possible, PROVIDED
//    events are started in increasing id order, which is how Geant4's
//    G4MTRunManager hands them out: whoever holds the next-needed event then
//    already exists and is running. A caller that submits a high id before the
//    low ids it also owns, from the same thread, would wait for itself. Do not
//    use the bound in that case.
//
// Failure
//  - If the consumer throws, the exception reaches the submitting thread that
//    ran it, the buffer is marked failed, and all current and future submit()
//    calls throw std::runtime_error. Nobody is left waiting.
template <typename T>
class ReorderBuffer {
 public:
  using Consumer = std::function<void(std::uint64_t, T&&)>;

  explicit ReorderBuffer(Consumer consumer, std::uint64_t first_id = 0, std::size_t max_ahead = 0)
      : consumer_(std::move(consumer)), next_(first_id), max_ahead_(max_ahead)
  {
  }

  ReorderBuffer(const ReorderBuffer&) = delete;
  ReorderBuffer& operator=(const ReorderBuffer&) = delete;

  void submit(std::uint64_t id, T item)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (failed_) {
      throw std::runtime_error("ReorderBuffer: the consumer failed earlier");
    }
    if (id < next_ || pending_.count(id) != 0) {
      throw std::logic_error("ReorderBuffer: event " + std::to_string(id) +
                             " was submitted twice");
    }
    if (max_ahead_ != 0) {
      window_.wait(lock, [&] { return failed_ || id < next_ + max_ahead_; });
      if (failed_) {
        throw std::runtime_error("ReorderBuffer: the consumer failed earlier");
      }
    }
    pending_.emplace(id, std::move(item));
    if (pending_.size() > high_water_) {
      high_water_ = pending_.size();
    }
    if (draining_) {
      return;  // the thread that is releasing events will pick this one up
    }

    draining_ = true;
    for (;;) {
      auto it = pending_.find(next_);
      if (it == pending_.end()) {
        break;
      }
      T value = std::move(it->second);
      pending_.erase(it);
      const std::uint64_t released = next_++;
      window_.notify_all();
      lock.unlock();
      try {
        consumer_(released, std::move(value));
      }
      catch (...) {
        lock.lock();
        failed_ = true;
        draining_ = false;
        window_.notify_all();
        throw;
      }
      lock.lock();
    }
    draining_ = false;
  }

  // The id the consumer will receive next; ids below it have been released.
  std::uint64_t next_id() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_;
  }

  // Events submitted but not yet released.
  std::size_t pending() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
  }

  // The largest value pending() has had.
  std::size_t high_water_mark() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return high_water_;
  }

 private:
  Consumer consumer_;
  mutable std::mutex mutex_;
  std::condition_variable window_;
  std::map<std::uint64_t, T> pending_;
  std::uint64_t next_;
  std::size_t max_ahead_;
  std::size_t high_water_ = 0;
  bool draining_ = false;
  bool failed_ = false;
};
