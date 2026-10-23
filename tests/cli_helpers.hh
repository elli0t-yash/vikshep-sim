#pragma once

// Helpers for tests that run the vikshep-sim executable (POSIX only).

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace cli {

struct Outcome {
  std::string out;  // stdout
  int code = -1;    // exit code
};

// Runs vikshep-sim with the arguments. stderr is discarded.
inline Outcome run(const std::string& args)
{
  const std::string command = std::string("\"") + VIKSHEP_SIM_EXE + "\" " + args + " 2>/dev/null";
  Outcome result;
  FILE* pipe = popen(command.c_str(), "r");
  REQUIRE(pipe != nullptr);
  char buffer[4096];
  std::size_t n = 0;
  while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0) {
    result.out.append(buffer, n);
  }
  const int status = pclose(pipe);
  result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

inline std::string example_path()
{
  return std::string(VIKSHEP_EXAMPLES_DIR) + "/calorimeter.json";
}

// A file in the temporary directory, removed when the object dies.
class TempFile {
 public:
  explicit TempFile(const std::string& text = "", const std::string& suffix = ".json")
  {
    static int counter = 0;
    path_ = std::filesystem::temp_directory_path() /
            ("vikshep-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++) +
             suffix);
    std::ofstream(path_, std::ios::binary) << text;
  }
  ~TempFile() { std::filesystem::remove(path_); }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  std::string path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

inline int line_count(const std::string& s)
{
  int n = 0;
  for (char c : s) {
    n += c == '\n' ? 1 : 0;
  }
  return n;
}

}  // namespace cli
