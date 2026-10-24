#include "cancellation.hh"

#include <signal.h>
#include <unistd.h>

namespace cancellation {

namespace {

// volatile sig_atomic_t: the only kind of object a signal handler may modify.
volatile sig_atomic_t g_signals = 0;
volatile sig_atomic_t g_requested = 0;

constexpr int kExitCancelled = 4;

void handler(int)
{
  g_signals = g_signals + 1;
  g_requested = 1;
  if (g_signals >= 2) {
    _exit(kExitCancelled);  // a second signal: out at once, no further output
  }
}

}  // namespace

void install_handlers()
{
  struct sigaction action;
  action.sa_handler = &handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &action, nullptr);
  sigaction(SIGINT, &action, nullptr);
}

bool requested()
{
  return g_requested != 0;
}

void request()
{
  g_requested = 1;
}

int signals_received()
{
  return g_signals;
}

}  // namespace cancellation
