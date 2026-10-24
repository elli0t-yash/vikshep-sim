#include <string>

#include <catch2/catch_test_macros.hpp>

extern "C" {
#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow_ipc.h"
}

#include "versions.hh"

TEST_CASE("the vendored nanoarrow is the pinned snapshot", "[vendored]")
{
  CHECK(std::string(NANOARROW_VERSION) == "0.10.0-SNAPSHOT");
  CHECK(std::string(ArrowNanoarrowVersion()) == NANOARROW_VERSION);
  CHECK(std::string(kNanoarrowCommit).size() == 40);
}

TEST_CASE("the vendored nanoarrow can write dictionary batches", "[vendored]")
{
  // The reason for pinning a snapshot: release 0.9.0 has no such function.
  auto* write_dictionary = &ArrowIpcWriterWriteDictionaryBatch;
  CHECK(write_dictionary != nullptr);
}
