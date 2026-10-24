#include "stdout_channel.hh"

#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>

#include "cancellation.hh"

namespace {

std::mutex g_mutex;
int g_fd = -1;
bool g_broken = false;

}  // namespace

void StdoutChannel::capture()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_fd >= 0) {
    return;
  }
  std::cout.flush();
  std::fflush(stdout);
  // The channel keeps the real stdout; fd 1 becomes a copy of stderr.
  const int real = ::dup(STDOUT_FILENO);
  if (real < 0) {
    return;  // cannot happen in practice; leave stdout as it is
  }
  ::dup2(STDERR_FILENO, STDOUT_FILENO);
  g_fd = real;
  // std::cout shares libc++'s stream object with the Geant4 libraries, so this reaches them.
  std::cout.rdbuf(std::cerr.rdbuf());
  ::signal(SIGPIPE, SIG_IGN);
}

bool StdoutChannel::captured()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_fd >= 0;
}

void StdoutChannel::write_line(const std::string& line)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_fd < 0 || g_broken) {
    return;
  }
  const std::string text = line + "\n";
  std::size_t done = 0;
  while (done < text.size()) {
    const ssize_t n = ::write(g_fd, text.data() + done, text.size() - done);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      g_broken = true;
      cancellation::request();  // nobody reads our output any more
      return;
    }
    done += static_cast<std::size_t>(n);
  }
}

bool StdoutChannel::broken()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_broken;
}
