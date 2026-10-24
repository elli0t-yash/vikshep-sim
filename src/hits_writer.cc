#include "hits_writer.hh"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

extern "C" {
#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow_ipc.h"
}

#include "errors.hh"
#include "sha3_256.hh"

namespace {

// Failures while writing bytes (the "writing ..." and "starting the file" steps) are I/O
// failures of the output; the others (building arrays) are internal.
void check(int code, const char* what, const ArrowError& error)
{
  if (code != 0) {
    const std::string text = std::string("hits.arrow: ") + what + " failed (" + std::strerror(code) +
                             "): " + error.message;
    const std::string step = what;
    if (step.rfind("writing", 0) == 0 || step.rfind("starting the file", 0) == 0) {
      throw WriteError(text);
    }
    throw std::runtime_error(text);
  }
}

// The byte sink handed to nanoarrow: writes to the file and feeds the hash.
struct Tee {
  std::FILE* file = nullptr;
  Sha3_256* hash = nullptr;
  std::uint64_t bytes = 0;
};

int tee_write(ArrowIpcOutputStream* stream, const void* buffer, int64_t size, int64_t* written,
              ArrowError* error)
{
  auto* tee = static_cast<Tee*>(stream->private_data);
  const std::size_t n = std::fwrite(buffer, 1, static_cast<std::size_t>(size), tee->file);
  if (n != static_cast<std::size_t>(size)) {
    const int e = errno != 0 ? errno : EIO;
    ArrowErrorSet(error, "writing the file failed: %s", std::strerror(e));
    return e;
  }
  tee->hash->update(buffer, n);
  tee->bytes += n;
  *written = size;
  return NANOARROW_OK;
}

void tee_release(ArrowIpcOutputStream* stream)
{
  stream->release = nullptr;  // the Tee is owned by HitsWriter::Impl
}

struct Column {
  const char* name;
  ArrowType type;
  const char* unit;  // field metadata "unit", or nullptr
};

// docs/HITS.md, section 2: the columns, in this order.
const Column kColumns[16] = {
    {"event_id", NANOARROW_TYPE_UINT64, nullptr}, {"volume", NANOARROW_TYPE_INT32, nullptr},
    {"copy_no", NANOARROW_TYPE_INT32, nullptr},   {"layer", NANOARROW_TYPE_INT32, nullptr},
    {"x", NANOARROW_TYPE_DOUBLE, "mm"},           {"y", NANOARROW_TYPE_DOUBLE, "mm"},
    {"z", NANOARROW_TYPE_DOUBLE, "mm"},           {"t", NANOARROW_TYPE_DOUBLE, "ns"},
    {"edep", NANOARROW_TYPE_DOUBLE, "MeV"},       {"px", NANOARROW_TYPE_DOUBLE, "MeV/c"},
    {"py", NANOARROW_TYPE_DOUBLE, "MeV/c"},       {"pz", NANOARROW_TYPE_DOUBLE, "MeV/c"},
    {"track_id", NANOARROW_TYPE_INT32, nullptr},  {"parent_id", NANOARROW_TYPE_INT32, nullptr},
    {"pdg", NANOARROW_TYPE_INT32, nullptr},       {"step", NANOARROW_TYPE_INT32, nullptr},
};

constexpr int kVolumeColumn = 1;
constexpr int kDictionaryId = 0;

// Sets one key/value as the metadata of a schema (docs/HITS.md, section 3).
void set_metadata(ArrowSchema* schema, const char* key, const char* value, ArrowError& error)
{
  ArrowBuffer buffer;
  ArrowBufferInit(&buffer);
  int rc = ArrowMetadataBuilderInit(&buffer, nullptr);
  if (rc == 0) rc = ArrowMetadataBuilderAppend(&buffer, ArrowCharView(key), ArrowCharView(value));
  if (rc == 0) rc = ArrowSchemaSetMetadata(schema, reinterpret_cast<const char*>(buffer.data));
  ArrowBufferReset(&buffer);
  check(rc, "setting metadata", error);
}

}  // namespace

struct HitsWriter::Impl {
  std::string path;
  std::vector<std::string> volumes;
  std::size_t batch_rows = 0;
  int dictionary_index_of_slab[2] = {-1, -1};  // slab type -> dictionary index, -1 if absent

  Sha3_256 hash;
  Tee tee;
  ArrowError error;
  ArrowSchema schema;
  ArrowArrayView view;
  ArrowIpcWriter writer;
  ArrowArray batch;
  bool schema_ready = false;
  bool view_ready = false;
  bool writer_ready = false;
  bool batch_open = false;
  bool finished = false;
  std::size_t rows_in_batch = 0;
  std::uint64_t rows = 0;
  std::uint64_t record_batches = 0;

  Impl() { error.message[0] = '\0'; }

  ~Impl()
  {
    if (batch_open) batch.release(&batch);
    if (view_ready) ArrowArrayViewReset(&view);
    if (writer_ready) ArrowIpcWriterReset(&writer);
    if (schema_ready) schema.release(&schema);
    if (tee.file != nullptr) std::fclose(tee.file);
  }

  void build_schema()
  {
    ArrowSchemaInit(&schema);
    schema_ready = true;
    check(ArrowSchemaSetTypeStruct(&schema, 16), "building the schema", error);
    for (int i = 0; i < 16; ++i) {
      ArrowSchema* column = schema.children[i];
      check(ArrowSchemaSetType(column, kColumns[i].type), "building the schema", error);
      check(ArrowSchemaSetName(column, kColumns[i].name), "building the schema", error);
      column->flags &= ~ARROW_FLAG_NULLABLE;  // no nulls anywhere
      if (kColumns[i].unit != nullptr) {
        set_metadata(column, "unit", kColumns[i].unit, error);
      }
      if (i == kVolumeColumn) {
        check(ArrowSchemaAllocateDictionary(column), "building the schema", error);
        check(ArrowSchemaInitFromType(column->dictionary, NANOARROW_TYPE_STRING),
              "building the schema", error);
      }
    }
    schema.flags &= ~ARROW_FLAG_NULLABLE;
    set_metadata(&schema, "vikshep.hits.schema_version", "1.0", error);
  }

  // The dictionary values (the sensitive volume names) appended to a string array.
  void append_dictionary(ArrowArray* values)
  {
    for (const std::string& name : volumes) {
      check(ArrowArrayAppendString(values, ArrowCharView(name.c_str())), "building the dictionary",
            error);
    }
  }

  void write_dictionary_batch()
  {
    ArrowSchema values_schema;
    ArrowSchemaInit(&values_schema);
    check(ArrowSchemaSetType(&values_schema, NANOARROW_TYPE_STRING), "building the dictionary", error);
    ArrowArray values;
    check(ArrowArrayInitFromSchema(&values, &values_schema, &error), "building the dictionary", error);
    check(ArrowArrayStartAppending(&values), "building the dictionary", error);
    append_dictionary(&values);
    check(ArrowArrayFinishBuildingDefault(&values, &error), "building the dictionary", error);
    ArrowArrayView values_view;
    check(ArrowArrayViewInitFromSchema(&values_view, &values_schema, &error), "building the dictionary",
          error);
    check(ArrowArrayViewSetArray(&values_view, &values, &error), "building the dictionary", error);
    // Written exactly once, before the first record batch (docs/HITS.md, section 1.2).
    const int rc = ArrowIpcWriterWriteDictionaryBatch(&writer, kDictionaryId, 0, &values_view, &error);
    ArrowArrayViewReset(&values_view);
    values.release(&values);
    values_schema.release(&values_schema);
    check(rc, "writing the dictionary", error);
  }

  void start_batch()
  {
    check(ArrowArrayInitFromSchema(&batch, &schema, &error), "starting a batch", error);
    batch_open = true;
    check(ArrowArrayStartAppending(&batch), "starting a batch", error);
    check(ArrowArrayStartAppending(batch.children[kVolumeColumn]->dictionary), "starting a batch",
          error);
    rows_in_batch = 0;
  }

  void flush_batch()
  {
    if (!batch_open || rows_in_batch == 0) {
      return;
    }
    // The dictionary values must be present in the array we hand over, but they are not
    // written again: the writer writes only the indices of a record batch.
    append_dictionary(batch.children[kVolumeColumn]->dictionary);
    check(ArrowArrayFinishBuildingDefault(&batch, &error), "finishing a batch", error);
    check(ArrowArrayViewSetArray(&view, &batch, &error), "finishing a batch", error);
    check(ArrowIpcWriterWriteArrayView(&writer, &view, &error), "writing a batch", error);
    batch.release(&batch);
    batch_open = false;
    rows_in_batch = 0;
    ++record_batches;
  }

  void append_hit(std::uint64_t event_id, const Hit& h)
  {
    if (h.volume < 0 || h.volume > 1 || dictionary_index_of_slab[h.volume] < 0) {
      throw std::invalid_argument("hits.arrow: a hit is in a volume that is not in the dictionary");
    }
    if (!batch_open) {
      start_batch();
    }
    ArrowArray** c = batch.children;
    int rc = 0;
    rc |= ArrowArrayAppendUInt(c[0], event_id);
    rc |= ArrowArrayAppendInt(c[1], dictionary_index_of_slab[h.volume]);
    rc |= ArrowArrayAppendInt(c[2], h.copy_no);
    rc |= ArrowArrayAppendInt(c[3], h.layer);
    rc |= ArrowArrayAppendDouble(c[4], h.x_mm);
    rc |= ArrowArrayAppendDouble(c[5], h.y_mm);
    rc |= ArrowArrayAppendDouble(c[6], h.z_mm);
    rc |= ArrowArrayAppendDouble(c[7], h.t_ns);
    rc |= ArrowArrayAppendDouble(c[8], h.edep_mev);
    rc |= ArrowArrayAppendDouble(c[9], h.px_mev);
    rc |= ArrowArrayAppendDouble(c[10], h.py_mev);
    rc |= ArrowArrayAppendDouble(c[11], h.pz_mev);
    rc |= ArrowArrayAppendInt(c[12], h.track_id);
    rc |= ArrowArrayAppendInt(c[13], h.parent_id);
    rc |= ArrowArrayAppendInt(c[14], h.pdg);
    rc |= ArrowArrayAppendInt(c[15], h.step);
    check(rc, "appending a hit", error);
    check(ArrowArrayFinishElement(&batch), "appending a hit", error);
    ++rows_in_batch;
    ++rows;
    if (rows_in_batch == batch_rows) {
      flush_batch();
    }
  }
};

HitsWriter::HitsWriter(const std::string& path, const std::vector<std::string>& volumes,
                       std::size_t batch_rows)
    : impl_(new Impl)
{
  Impl& m = *impl_;
  if (batch_rows == 0) {
    throw std::invalid_argument("hits.arrow: batch_rows must be positive");
  }
  m.path = path;
  m.volumes = volumes;
  m.batch_rows = batch_rows;
  for (std::size_t i = 0; i < volumes.size(); ++i) {
    if (i > 0 && !(volumes[i - 1] < volumes[i])) {
      throw std::invalid_argument("hits.arrow: volumes must be sorted and without repeats");
    }
    if (volumes[i] == "absorber") {
      m.dictionary_index_of_slab[0] = static_cast<int>(i);
    }
    else if (volumes[i] == "active") {
      m.dictionary_index_of_slab[1] = static_cast<int>(i);
    }
    else {
      throw std::invalid_argument("hits.arrow: unknown volume " + volumes[i]);
    }
  }

  m.tee.file = std::fopen(path.c_str(), "wb");
  if (m.tee.file == nullptr) {
    throw WriteError("hits.arrow: cannot create the file: " + std::string(std::strerror(errno)));
  }
  m.tee.hash = &m.hash;

  m.build_schema();
  check(ArrowArrayViewInitFromSchema(&m.view, &m.schema, &m.error), "preparing the writer", m.error);
  m.view_ready = true;

  ArrowIpcOutputStream stream;
  stream.write = &tee_write;
  stream.release = &tee_release;
  stream.private_data = &m.tee;
  check(ArrowIpcWriterInit(&m.writer, &stream), "preparing the writer", m.error);
  m.writer_ready = true;

  check(ArrowIpcWriterStartFile(&m.writer, &m.error), "starting the file", m.error);
  check(ArrowIpcWriterWriteSchema(&m.writer, &m.schema, &m.error), "writing the schema", m.error);
  m.write_dictionary_batch();
}

HitsWriter::~HitsWriter() = default;

void HitsWriter::add_event(const EventHits& event)
{
  if (impl_->finished) {
    throw std::logic_error("hits.arrow: add_event after finish");
  }
  for (const Hit& hit : event.hits) {
    impl_->append_hit(event.event_id, hit);
  }
}

HitsFileSummary HitsWriter::finish()
{
  Impl& m = *impl_;
  if (m.finished) {
    throw std::logic_error("hits.arrow: finish called twice");
  }
  m.finished = true;

  m.flush_batch();
  // End-of-stream marker before the footer, as the file format description has it.
  check(ArrowIpcWriterWriteArrayView(&m.writer, nullptr, &m.error), "writing the end marker", m.error);
  check(ArrowIpcWriterFinalizeFile(&m.writer, &m.error), "writing the footer", m.error);

  const bool flushed = std::fflush(m.tee.file) == 0;
  const bool closed = std::fclose(m.tee.file) == 0;
  m.tee.file = nullptr;
  if (!flushed || !closed) {
    throw WriteError("hits.arrow: closing the file failed: " + std::string(std::strerror(errno)));
  }

  HitsFileSummary summary;
  summary.rows = m.rows;
  summary.record_batches = m.record_batches;
  summary.bytes = m.tee.bytes;
  const auto digest = m.hash.finish();
  summary.oid = oid_of(digest);

  // Verification: hash the file as it is on disk and compare with the hash computed
  // while writing. A mismatch means bytes were lost or changed on the way.
  struct stat st;
  if (::stat(m.path.c_str(), &st) != 0 || static_cast<std::uint64_t>(st.st_size) != summary.bytes) {
    throw IntegrityError("hits.arrow: the file on disk has a different size than what was written");
  }
  const auto again = sha3_256_of_file(m.path);
  if (again != digest) {
    throw IntegrityError("hits.arrow: the hash of the file on disk differs from the hash computed "
                         "while writing it; refusing to produce an OID");
  }
  return summary;
}
