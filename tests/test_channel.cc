// The stdout channel and the signal handlers, through the test-only channel-probe (POSIX only).

#include <cstdlib>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "cli_helpers.hh"

namespace {

cli::Outcome probe(const std::string& mode, const std::string& redirects)
{
  cli::Outcome result;
  FILE* pipe = popen((std::string("\"") + VIKSHEP_CHANNEL_PROBE + "\" " + mode + " " + redirects).c_str(), "r");
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

}  // namespace

TEST_CASE("after capture, only the channel reaches stdout", "[channel]")
{
  const cli::Outcome out = probe("channel", "2>/dev/null");
  CHECK(out.code == 0);
  // The probe printed noise by printf, puts, std::cout and write(1): none of it is here.
  CHECK(out.out == "{\"event\":\"progress\",\"done\":0,\"total\":1}\n");
}

TEST_CASE("after capture, the noise goes to stderr", "[channel]")
{
  const cli::Outcome err = probe("channel", "2>&1 >/dev/null");  // stderr only
  CHECK(err.out.find("noise by printf") != std::string::npos);
  CHECK(err.out.find("noise by puts") != std::string::npos);
  CHECK(err.out.find("noise by std::cout") != std::string::npos);
  CHECK(err.out.find("noise by write(1)") != std::string::npos);
  CHECK(err.out.find("text on stderr") != std::string::npos);
  CHECK(err.out.find("\"event\"") == std::string::npos);  // the channel line is not on stderr
}

TEST_CASE("a second signal ends the process with exit code 4", "[channel]")
{
  const cli::Outcome r = probe("signal", "2>&1");
  CHECK(r.out.find("first signal handled, process continues") != std::string::npos);
  CHECK(r.code == 4);
}

TEST_CASE("a closed reader makes the channel give up and request cancellation", "[channel]")
{
  // head -c 0 reads nothing and exits, so the probe's later writes hit a closed pipe.
  // (PIPESTATUS needs bash.)
  const int status = std::system(("bash -c '" + std::string("\"") + VIKSHEP_CHANNEL_PROBE +
                                  "\" closed 2>/dev/null | head -c 0; exit ${PIPESTATUS[0]}'").c_str());
  CHECK(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 0);
}
