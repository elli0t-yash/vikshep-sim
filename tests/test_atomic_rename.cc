#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

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
#if defined(__APPLE__) || defined(__linux__)
  INFO("this platform has a no-overwrite rename call; file systems that lack it use the fallback");
  (void)atomic;
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
  rename_no_replace(from, to, ec);
  CHECK(ec == std::errc::file_exists);
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
