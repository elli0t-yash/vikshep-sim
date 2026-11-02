#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "byte_compare.hh"
#include "cli_helpers.hh"
#include "hits_writer.hh"
#include "sha3_256.hh"
#include "versions.hh"

namespace {

std::string read_file(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A hit whose values are all exactly representable, so the bytes are the same on
// every platform that follows IEEE 754.
Hit make_hit(int volume, int layer, int k)
{
  Hit h;
  h.volume = volume;
  h.copy_no = layer;
  h.layer = layer;
  h.x_mm = 0.25 * k;
  h.y_mm = -0.5 * k;
  h.z_mm = -75.0 + 1.5 * k;
  h.t_ns = 0.125 * k;
  h.edep_mev = 0.0625 * (k + 1);
  h.px_mev = 8.0 * k;
  h.py_mev = -2.0;
  h.pz_mev = 100.0 + k;
  h.track_id = 1 + k % 3;
  h.parent_id = k % 3 == 0 ? 0 : 1;
  h.pdg = k % 2 == 0 ? 11 : 22;
  h.step = 1 + k;
  return h;
}

EventHits event(std::uint64_t id, std::vector<Hit> hits)
{
  EventHits e;
  e.event_id = id;
  e.hits = std::move(hits);
  return e;
}

// Five events, one without hits, both volumes, ten rows in all.
std::vector<EventHits> small_dataset()
{
  return {
      event(0, {make_hit(0, 0, 0), make_hit(1, 0, 1), make_hit(1, 1, 2)}),
      event(1, {}),  // a hit-free event: absent from the file
      event(2, {make_hit(0, 2, 3), make_hit(0, 2, 4), make_hit(1, 3, 5), make_hit(1, 3, 6)}),
      event(3, {}),
      event(4, {make_hit(0, 9, 7), make_hit(1, 9, 8), make_hit(1, 9, 9)}),
  };
}

HitsFileSummary write_file(const std::string& path, const std::vector<EventHits>& events,
                           const std::vector<std::string>& volumes, std::size_t batch_rows)
{
  HitsWriter writer(path, volumes, batch_rows);
  for (const EventHits& e : events) {
    writer.add_event(e);
  }
  return writer.finish();
}

// OID of a golden file as recorded in tests/golden/OIDS.txt (computed with Python's hashlib).
std::string recorded_oid(const std::string& golden_name)
{
  std::ifstream in(std::string(VIKSHEP_GOLDEN_DIR) + "/OIDS.txt");
  std::string name, oid;
  while (in >> name >> oid) {
    if (name == golden_name) {
      return oid;
    }
  }
  return "";
}

const char* kGoldenMessage =
    "The bytes written for a fixed dataset differ from the checked-in golden file. A change "
    "in the writer (the nanoarrow code, how it is used, or the column layout) changes the bytes "
    "of every hits.arrow and therefore every OID. If the change is intended: bump kWriterScheme "
    "in src/versions.hh, describe it in docs/HITS.md, regenerate the golden files by running "
    "this test with VIKSHEP_UPDATE_GOLDEN=1 set, then refresh tests/golden/OIDS.txt with "
    "'python3 tests/check_golden.py tests/golden --write-oids' and check them with "
    "'python3 tests/check_golden.py tests/golden'.";

// Compares the file with the golden file, or rewrites the golden file when asked to.
void compare_with_golden(const std::string& written, const std::string& golden_name)
{
  const std::string golden_path = std::string(VIKSHEP_GOLDEN_DIR) + "/" + golden_name;
  if (std::getenv("VIKSHEP_UPDATE_GOLDEN") != nullptr) {
    std::filesystem::copy_file(written, golden_path, std::filesystem::copy_options::overwrite_existing);
    WARN("golden file rewritten: " << golden_name);
    return;
  }
  const std::string expected = read_file(golden_path);
  REQUIRE_FALSE(expected.empty());
  const std::string actual = read_file(written);
  INFO(kGoldenMessage);
  INFO("golden file " << golden_name << ": " << expected.size() << " bytes, written: " << actual.size()
                      << " bytes");
  CHECK_SAME_BYTES(actual, expected);
}

}  // namespace

TEST_CASE("golden file: small dataset, small batches, both volumes, a hit-free event", "[writer][golden]")
{
  const cli::TempFile file("", ".arrow");
  const HitsFileSummary summary =
      write_file(file.path(), small_dataset(), {"absorber", "active"}, /*batch_rows=*/4);
  CHECK(summary.rows == 10);
  CHECK(summary.record_batches == 3);  // 4 + 4 + 2 rows, events straddle the batches
  compare_with_golden(file.path(), "hits_small.arrow");
  // The OID of the golden file, computed independently with Python's hashlib.sha3_256.
  CHECK(summary.oid == recorded_oid("hits_small.arrow"));
}

TEST_CASE("golden file: only hit-free events", "[writer][golden]")
{
  const cli::TempFile file("", ".arrow");
  const std::vector<EventHits> events = {event(0, {}), event(1, {}), event(2, {})};
  const HitsFileSummary summary = write_file(file.path(), events, {"active"}, 4);
  CHECK(summary.rows == 0);
  CHECK(summary.record_batches == 0);
  compare_with_golden(file.path(), "hits_empty.arrow");
  CHECK(summary.oid == recorded_oid("hits_empty.arrow"));
}

TEST_CASE("the same data gives the same bytes and the same OID", "[writer]")
{
  const cli::TempFile a("", ".arrow");
  const cli::TempFile b("", ".arrow");
  const auto s1 = write_file(a.path(), small_dataset(), {"absorber", "active"}, 4);
  const auto s2 = write_file(b.path(), small_dataset(), {"absorber", "active"}, 4);
  {
    const std::string first_file = read_file(a.path());
    const std::string second_file = read_file(b.path());
    CHECK_SAME_BYTES(first_file, second_file);
  }
  CHECK(s1.oid == s2.oid);
  CHECK(s1.bytes == read_file(a.path()).size());
  CHECK(s1.oid == oid_of_bytes(read_file(a.path())));
  CHECK(s1.oid.size() == 28);
}

namespace {

// Fills 128 KB of stack below the caller with `fill` and returns, so that the stack the next calls use
// holds that value instead of whatever was there. Not inlined, and written through volatile so the
// compiler cannot drop it.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void poison_stack(unsigned char fill)
{
  volatile unsigned char buffer[1 << 17];
  for (std::size_t i = 0; i < sizeof buffer; ++i) {
    buffer[i] = fill;
  }
}

std::string write_with_poisoned_stack(const std::string& path, unsigned char fill)
{
  HitsWriter writer(path, {"absorber", "active"}, 4);
  for (const EventHits& e : small_dataset()) {
    poison_stack(fill);
    writer.add_event(e);
  }
  poison_stack(fill);  // the footer is built in finish(): its stack frames land on the poisoned stack
  writer.finish();
  return read_file(path);
}

}  // namespace

TEST_CASE("the bytes of hits.arrow do not depend on what was on the stack", "[writer][uninitialised]")
{
  // Regression test: the footer's Block structs have 4 bytes of padding, and the vendored nanoarrow once
  // copied them uninitialised, so the file contained stack bytes (see third_party/nanoarrow/PATCHES.md).
  // That depended on the compiler, the optimisation level and the platform, so a plain golden-file test
  // could pass or fail by luck. Poisoning the stack makes any such leak show up everywhere.
  const std::string golden = read_file(std::string(VIKSHEP_GOLDEN_DIR) + "/hits_small.arrow");
  for (unsigned char fill : {0x00, 0x55, 0xAA, 0xFF}) {
    INFO("stack filled with 0x" << std::hex << static_cast<int>(fill));
    const cli::TempFile file("", ".arrow");
    const std::string produced = write_with_poisoned_stack(file.path(), fill);
    CHECK_SAME_BYTES(produced, golden);
  }
}

TEST_CASE("the batch size changes the bytes, as HITS.md says", "[writer]")
{
  const cli::TempFile a("", ".arrow");
  const cli::TempFile b("", ".arrow");
  const auto s1 = write_file(a.path(), small_dataset(), {"absorber", "active"}, 4);
  const auto s2 = write_file(b.path(), small_dataset(), {"absorber", "active"}, 5);
  CHECK(s1.oid != s2.oid);
}

TEST_CASE("batches are cut by row count only, with the default size 65536", "[writer]")
{
  std::vector<Hit> hits;
  for (int i = 0; i < 70000; ++i) {
    hits.push_back(make_hit(i % 2, i % 10, i % 100));
  }
  const std::vector<EventHits> events = {event(0, hits), event(1, {make_hit(0, 0, 0)})};
  const cli::TempFile file("", ".arrow");
  HitsWriter writer(file.path(), {"absorber", "active"});  // default batch size
  for (const auto& e : events) {
    writer.add_event(e);
  }
  const HitsFileSummary summary = writer.finish();
  CHECK(summary.rows == 70001);
  CHECK(summary.record_batches == 2);  // 65536 + 4465
}

TEST_CASE("the dictionary holds only the sensitive volumes", "[writer]")
{
  const cli::TempFile file("", ".arrow");
  // Only "active" is sensitive: its dictionary index is 0 (checked in the pyarrow test).
  const std::vector<EventHits> events = {event(0, {make_hit(1, 0, 0)})};
  const auto summary = write_file(file.path(), events, {"active"}, 4);
  CHECK(summary.rows == 1);
}

TEST_CASE("misuse is rejected", "[writer]")
{
  const cli::TempFile file("", ".arrow");
  CHECK_THROWS_AS(HitsWriter(file.path(), {"active", "absorber"}, 4), std::invalid_argument);  // unsorted
  CHECK_THROWS_AS(HitsWriter(file.path(), {"active", "active"}, 4), std::invalid_argument);
  CHECK_THROWS_AS(HitsWriter(file.path(), {"gap"}, 4), std::invalid_argument);
  CHECK_THROWS_AS(HitsWriter(file.path(), {"active"}, 0), std::invalid_argument);
  CHECK_THROWS_AS(HitsWriter("/nonexistent-directory/hits.arrow", {"active"}, 4), std::runtime_error);

  HitsWriter writer(file.path(), {"active"}, 4);
  // A hit in a volume that is not in the dictionary.
  CHECK_THROWS_AS(writer.add_event(event(0, {make_hit(0, 0, 0)})), std::invalid_argument);
}

TEST_CASE("add_event and finish after finish are errors", "[writer]")
{
  const cli::TempFile file("", ".arrow");
  HitsWriter writer(file.path(), {"active"}, 4);
  writer.finish();
  CHECK_THROWS_AS(writer.add_event(event(0, {})), std::logic_error);
  CHECK_THROWS_AS(writer.finish(), std::logic_error);
}

TEST_CASE("the writer scheme is 1", "[writer]")
{
  // Bump together with the golden files (see kGoldenMessage).
  CHECK(kWriterScheme == 1);
}

TEST_CASE("the tracker volume is a dictionary entry like the calorimeter's", "[writer]")
{
  const cli::TempFile a("", ".arrow");
  const cli::TempFile b("", ".arrow");
  // Hits of a tracker run: volume 2 is "tracker_layer", copy number and layer are the layer index.
  const std::vector<EventHits> events = {
      event(0, {make_hit(kVolumeTrackerLayer, 0, 0), make_hit(kVolumeTrackerLayer, 1, 1)}),
      event(1, {}),
      event(2, {make_hit(kVolumeTrackerLayer, 5, 2)}),
  };
  const auto first = write_file(a.path(), events, {"tracker_layer"}, 4);
  const auto second = write_file(b.path(), events, {"tracker_layer"}, 4);
  CHECK(first.rows == 3);
  CHECK(first.oid == second.oid);
  CHECK(read_file(a.path()) == read_file(b.path()));

  // The full dictionary of all templates is sorted bytewise: absorber, active, tracker_layer.
  CHECK(std::string(kVolumeNames[0]) < std::string(kVolumeNames[1]));
  CHECK(std::string(kVolumeNames[1]) < std::string(kVolumeNames[2]));
  const cli::TempFile c("", ".arrow");
  const std::vector<EventHits> all = {event(0, {make_hit(0, 0, 0), make_hit(1, 0, 1), make_hit(2, 0, 2)})};
  CHECK(write_file(c.path(), all, {"absorber", "active", "tracker_layer"}, 4).rows == 3);
}

TEST_CASE("a hit in a volume that is not in the dictionary is rejected, for every template", "[writer]")
{
  const cli::TempFile file("", ".arrow");
  HitsWriter tracker(file.path(), {"tracker_layer"}, 4);
  CHECK_THROWS_AS(tracker.add_event(event(0, {make_hit(kVolumeActive, 0, 0)})), std::invalid_argument);
  CHECK_THROWS_AS(tracker.add_event(event(0, {make_hit(kVolumeCount, 0, 0)})), std::invalid_argument);
  CHECK_THROWS_AS(tracker.add_event(event(0, {make_hit(-1, 0, 0)})), std::invalid_argument);
  const cli::TempFile other("", ".arrow");
  HitsWriter calorimeter(other.path(), {"absorber", "active"}, 4);
  CHECK_THROWS_AS(calorimeter.add_event(event(0, {make_hit(kVolumeTrackerLayer, 0, 0)})), std::invalid_argument);
}
