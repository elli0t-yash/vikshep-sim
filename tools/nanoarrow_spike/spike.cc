// Feasibility spike: write the HITS.md schema as an Arrow IPC file with nanoarrow.
// Usage: spike <out.arrow> <rows> [dict|nodict] [nodictbatch]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
extern "C" {
#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow_ipc.h"
}

#define CHECK(expr)                                                                 \
  do {                                                                              \
    int _rc = (expr);                                                               \
    if (_rc != 0) {                                                                 \
      std::fprintf(stderr, "FAILED %s:%d: %s -> %d (%s)\n", __FILE__, __LINE__, #expr, _rc, err.message); \
      return 1;                                                                     \
    }                                                                               \
  } while (0)

static int set_unit(ArrowSchema* s, const char* unit)
{
  ArrowBuffer b;
  ArrowBufferInit(&b);
  if (ArrowMetadataBuilderInit(&b, nullptr) != 0) return 1;
  if (ArrowMetadataBuilderAppend(&b, ArrowCharView("unit"), ArrowCharView(unit)) != 0) return 1;
  int rc = ArrowSchemaSetMetadata(s, reinterpret_cast<const char*>(b.data));
  ArrowBufferReset(&b);
  return rc;
}

int main(int argc, char** argv)
{
  ArrowError err;
  err.message[0] = 0;
  const char* path = argv[1];
  const int rows_total = std::atoi(argv[2]);
  const bool dict = argc > 3 ? std::strcmp(argv[3], "nodict") != 0 : true;
  const int batch_rows = 65536;

  // ---- schema
  ArrowSchema schema;
  ArrowSchemaInit(&schema);
  CHECK(ArrowSchemaSetTypeStruct(&schema, 16));
  struct Col { const char* name; ArrowType type; const char* unit; };
  const Col cols[16] = {
      {"event_id", NANOARROW_TYPE_UINT64, nullptr}, {"volume", NANOARROW_TYPE_INT32, nullptr},
      {"copy_no", NANOARROW_TYPE_INT32, nullptr},   {"layer", NANOARROW_TYPE_INT32, nullptr},
      {"x", NANOARROW_TYPE_DOUBLE, "mm"},           {"y", NANOARROW_TYPE_DOUBLE, "mm"},
      {"z", NANOARROW_TYPE_DOUBLE, "mm"},           {"t", NANOARROW_TYPE_DOUBLE, "ns"},
      {"edep", NANOARROW_TYPE_DOUBLE, "MeV"},       {"px", NANOARROW_TYPE_DOUBLE, "MeV/c"},
      {"py", NANOARROW_TYPE_DOUBLE, "MeV/c"},       {"pz", NANOARROW_TYPE_DOUBLE, "MeV/c"},
      {"track_id", NANOARROW_TYPE_INT32, nullptr},  {"parent_id", NANOARROW_TYPE_INT32, nullptr},
      {"pdg", NANOARROW_TYPE_INT32, nullptr},       {"step", NANOARROW_TYPE_INT32, nullptr}};
  for (int i = 0; i < 16; ++i) {
    ArrowSchema* c = schema.children[i];
    CHECK(ArrowSchemaSetType(c, cols[i].type));
    CHECK(ArrowSchemaSetName(c, cols[i].name));
    c->flags &= ~ARROW_FLAG_NULLABLE;
    if (cols[i].unit) CHECK(set_unit(c, cols[i].unit));
    if (i == 1 && dict) {
      CHECK(ArrowSchemaAllocateDictionary(c));
      CHECK(ArrowSchemaInitFromType(c->dictionary, NANOARROW_TYPE_STRING));
    }
  }
  schema.flags &= ~ARROW_FLAG_NULLABLE;
  {
    ArrowBuffer b;
    ArrowBufferInit(&b);
    CHECK(ArrowMetadataBuilderInit(&b, nullptr));
    CHECK(ArrowMetadataBuilderAppend(&b, ArrowCharView("vikshep.hits.schema_version"), ArrowCharView("1.0")));
    CHECK(ArrowSchemaSetMetadata(&schema, reinterpret_cast<const char*>(b.data)));
    ArrowBufferReset(&b);
  }

  // ---- writer
  FILE* f = std::fopen(path, "wb");
  ArrowIpcOutputStream out;
  CHECK(ArrowIpcOutputStreamInitFile(&out, f, /*close_on_release=*/1));
  ArrowIpcWriter writer;
  CHECK(ArrowIpcWriterInit(&writer, &out));
  CHECK(ArrowIpcWriterStartFile(&writer, &err));
  CHECK(ArrowIpcWriterWriteSchema(&writer, &schema, &err));

#ifdef HAVE_DICT_BATCH
  if (dict) {
    // The dictionary, written exactly once, before the first record batch.
    ArrowArray values;
    ArrowSchema values_schema;
    ArrowSchemaInit(&values_schema);
    CHECK(ArrowSchemaSetType(&values_schema, NANOARROW_TYPE_STRING));
    CHECK(ArrowArrayInitFromSchema(&values, &values_schema, &err));
    CHECK(ArrowArrayStartAppending(&values));
    CHECK(ArrowArrayAppendString(&values, ArrowCharView("absorber")));
    CHECK(ArrowArrayAppendString(&values, ArrowCharView("active")));
    CHECK(ArrowArrayFinishBuildingDefault(&values, &err));
    ArrowArrayView values_view;
    CHECK(ArrowArrayViewInitFromSchema(&values_view, &values_schema, &err));
    CHECK(ArrowArrayViewSetArray(&values_view, &values, &err));
    CHECK(ArrowIpcWriterWriteDictionaryBatch(&writer, 0, 0, &values_view, &err));
    ArrowArrayViewReset(&values_view);
    values.release(&values);
    values_schema.release(&values_schema);
  }
#endif

  // ---- data, in batches
  int written = 0;
  while (written < rows_total) {
    const int n = std::min(batch_rows, rows_total - written);
    ArrowArray array;
    CHECK(ArrowArrayInitFromSchema(&array, &schema, &err));
    CHECK(ArrowArrayStartAppending(&array));
    if (dict) {
      CHECK(ArrowArrayStartAppending(array.children[1]->dictionary));
    }
    for (int r = 0; r < n; ++r) {
      const int row = written + r;
      CHECK(ArrowArrayAppendUInt(array.children[0], row / 3));
      CHECK(ArrowArrayAppendInt(array.children[1], row % 2));
      CHECK(ArrowArrayAppendInt(array.children[2], row % 10));
      CHECK(ArrowArrayAppendInt(array.children[3], row % 10));
      for (int c = 4; c < 12; ++c) CHECK(ArrowArrayAppendDouble(array.children[c], row * 0.5 + c));
      for (int c = 12; c < 16; ++c) CHECK(ArrowArrayAppendInt(array.children[c], row + c));
      CHECK(ArrowArrayFinishElement(&array));
    }
    if (dict) {
      CHECK(ArrowArrayAppendString(array.children[1]->dictionary, ArrowCharView("absorber")));
      CHECK(ArrowArrayAppendString(array.children[1]->dictionary, ArrowCharView("active")));
    }
    CHECK(ArrowArrayFinishBuildingDefault(&array, &err));
    ArrowArrayView view;
    CHECK(ArrowArrayViewInitFromSchema(&view, &schema, &err));
    CHECK(ArrowArrayViewSetArray(&view, &array, &err));
    CHECK(ArrowIpcWriterWriteArrayView(&writer, &view, &err));
    ArrowArrayViewReset(&view);
    array.release(&array);
    written += n;
  }
#ifdef WITH_EOS
  CHECK(ArrowIpcWriterWriteArrayView(&writer, nullptr, &err));  // end-of-stream marker
#endif
  CHECK(ArrowIpcWriterFinalizeFile(&writer, &err));
  ArrowIpcWriterReset(&writer);
  schema.release(&schema);
  std::printf("wrote %d rows\n", rows_total);
  return 0;
}
