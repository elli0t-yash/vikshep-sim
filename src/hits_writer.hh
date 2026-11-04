#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hits.hh"

// Writes hits.arrow as specified in docs/HITS.md: an Arrow IPC file with a footer,
// uncompressed, no non-deterministic metadata, the columns of section 2, one
// dictionary for `volume` written once, record batches of a fixed number of rows,
// an end-of-stream marker before the footer.
//
// Memory stays bounded: rows are appended to the batch being built, and the batch is
// written out as soon as it holds `batch_rows` rows. The SHA3-256 hash of the bytes
// is computed while they are written, and checked at the end by reading the file back.
struct HitsFileSummary {
  std::uint64_t rows = 0;            // hits written
  std::uint64_t record_batches = 0;
  std::uint64_t bytes = 0;           // size of the file
  std::string oid;                   // 28 lowercase hex characters (docs/HITS.md, section 6)
};

class HitsWriter {
 public:
  static constexpr std::size_t kDefaultBatchRows = 65536;  // docs/HITS.md, section 1.1

  // volumes: the names of the sensitive volumes, sorted bytewise, without repeats
  // (the dictionary, docs/HITS.md section 1.2). Each must be one of kVolumeNames (hits.hh).
  // batch_rows: only tests use another value than the default.
  // Throws std::runtime_error if the file cannot be created.
  HitsWriter(const std::string& path, const std::vector<std::string>& volumes,
             std::size_t batch_rows = kDefaultBatchRows);
  ~HitsWriter();
  HitsWriter(const HitsWriter&) = delete;
  HitsWriter& operator=(const HitsWriter&) = delete;

  // Appends the hits of one event. Events must arrive in event_id order (the caller's
  // job; the reorder buffer does it). An event without hits writes nothing.
  // Throws std::runtime_error on a write error, std::invalid_argument for a hit in a
  // volume that is not in the dictionary.
  void add_event(const EventHits& event);

  // Writes the last batch, the end-of-stream marker and the footer, closes the file,
  // and verifies the hash by reading the file back. Throws std::runtime_error if
  // anything fails or the hashes differ. Call once.
  HitsFileSummary finish();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
