#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "atomic_rename.hh"
#include "cli_helpers.hh"

namespace fs = std::filesystem;

TEST_CASE("rename_no_replace moves a folder to a new name", "[rename]")
{
  cli::TempDir dir;
  const fs::path from = dir.out("from"), to = dir.out("to");
  fs::create_directories(from);
  std::ofstream(from / "file.txt") << "content";
  std::error_code ec;
  const bool atomic = rename_no_replace(from, to, ec);
  CHECK_FALSE(ec);
  // Printed even when the test passes (visible in the CI log with ctest -V): which call was used.
  WARN("rename_no_replace mechanism: " << rename_no_replace_mechanism() << "; the atomic call was "
       << (atomic ? "taken" : "NOT taken (fallback used)"));
#if defined(__APPLE__) || defined(__linux__)
  // On macOS and Linux the no-overwrite call must be the one that ran (on the file systems of a
  // temporary directory: APFS, ext4, tmpfs).
  CHECK(atomic);
  CHECK(std::string(rename_no_replace_mechanism()) != "check-then-rename (fallback)");
#endif
  CHECK_FALSE(fs::exists(from));
  CHECK(fs::exists(to / "file.txt"));
}

TEST_CASE("rename_no_replace never replaces an existing folder, not even an empty one", "[rename]")
{
  cli::TempDir dir;
  const fs::path from = dir.out("from"), to = dir.out("to");
  fs::create_directories(from);
  std::ofstream(from / "new.txt") << "new";
  fs::create_directories(to);  // an empty folder: an ordinary rename would replace it
  std::error_code ec;
  const bool atomic = rename_no_replace(from, to, ec);
  CHECK(ec == std::errc::file_exists);
#if defined(__APPLE__) || defined(__linux__)
  CHECK(atomic);  // refused by the operating system (EEXIST), not by a check of ours
#else
  (void)atomic;
#endif
  CHECK(fs::exists(from / "new.txt"));  // the source is untouched
  CHECK(fs::is_empty(to));              // so is the target

  std::ofstream(dir.out("file")) << "x";
  rename_no_replace(from, dir.out("file"), ec);
  CHECK(ec);  // an existing file: refused as well
  CHECK(fs::exists(from / "new.txt"));
}

TEST_CASE("rename_no_replace reports other errors", "[rename]")
{
  cli::TempDir dir;
  std::error_code ec;
  rename_no_replace(dir.out("missing"), dir.out("to"), ec);
  CHECK(ec);
  CHECK(ec != std::errc::file_exists);
  CHECK_FALSE(fs::exists(dir.out("to")));
}
