// Test-only program for the stdout channel and the signal handlers.
//   channel-probe channel   writes noise by every route plus one channel line
//   channel-probe signal    raises SIGTERM twice at itself; the second must end it with exit 4
//   channel-probe closed    writes channel lines to a closed pipe (run with stdout closed by the reader)
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>

#include "cancellation.hh"
#include "stdout_channel.hh"

int main(int argc, char** argv)
{
  const char* mode = argc > 1 ? argv[1] : "";
  if (std::strcmp(mode, "channel") == 0) {
    StdoutChannel::capture();
    std::printf("noise by printf\n");
    std::fflush(stdout);
    std::puts("noise by puts");
    std::fflush(stdout);
    std::cout << "noise by std::cout" << std::endl;
    const char text[] = "noise by write(1)\n";
    if (::write(1, text, sizeof text - 1) < 0) return 3;
    std::cerr << "text on stderr\n";
    StdoutChannel::write_line("{\"event\":\"progress\",\"done\":0,\"total\":1}");
    return 0;
  }
  if (std::strcmp(mode, "signal") == 0) {
    cancellation::install_handlers();
    if (cancellation::requested()) return 10;
    ::raise(SIGTERM);
    if (!cancellation::requested() || cancellation::signals_received() != 1) return 11;
    std::fprintf(stderr, "first signal handled, process continues\n");
    ::raise(SIGINT);  // the second signal ends the process with exit code 4 inside the handler
    return 12;        // not reached
  }
  if (std::strcmp(mode, "closed") == 0) {
    StdoutChannel::capture();
    for (int i = 0; i < 3; ++i) {
      StdoutChannel::write_line("{\"event\":\"progress\",\"done\":0,\"total\":1}");
    }
    return StdoutChannel::broken() && cancellation::requested() ? 0 : 20;
  }
  return 2;
}
