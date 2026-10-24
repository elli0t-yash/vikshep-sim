#include "geant4_errors.hh"

#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>
#include <nlohmann/json.hpp>

#include "G4StateManager.hh"
#include "G4VExceptionHandler.hh"
#include "stdout_channel.hh"

namespace {

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_jsonl{false};

std::mutex g_folder_mutex;
std::string g_folder;

// Only one thread gets to end the process; any other thread that meets a fatal exception at the
// same moment waits here until the process is gone.
std::atomic_flag g_exiting = ATOMIC_FLAG_INIT;

class RunExceptionHandler : public G4VExceptionHandler {
 public:
  G4bool Notify(const char* origin, const char* code, G4ExceptionSeverity severity,
                const char* description) override
  {
    const std::string where = std::string(origin ? origin : "?") + " [" + (code ? code : "?") + "]";
    const std::string what = description ? description : "";

    if (severity == JustWarning || severity == IgnoreTheIssue) {
      // The run goes on. Short note on stderr, like Geant4's own warning.
      std::cerr << "G4Exception warning in " << where << ": " << what << std::endl;
      return false;
    }

    // FatalException, FatalErrorInArgument, RunMustBeAborted and EventMustBeAborted all end the
    // run: an aborted event would otherwise silently leave a hole in the output.
    if (g_exiting.test_and_set()) {
      for (;;) {
        ::sleep(60);  // another thread is ending the process
      }
    }
    std::cerr << "vikshep-sim: Geant4 fatal exception in " << where << ": " << what << std::endl;

    // No output folder may be left behind.
    clear_and_remove_folder();

    // One error line, then out at once. _Exit ends the process without running destructors or
    // atexit handlers (other threads are still running) and without abort()'s core dump.
    if (g_jsonl.load()) {
      StdoutChannel::write_line(
          nlohmann::json({{"code", "SIMULATION_FAILED"}, {"event", "error"}, {"message", "Geant4 reported a fatal error"}})
              .dump());
    }
    std::_Exit(1);
  }

 private:
  static void clear_and_remove_folder()
  {
    std::string folder;
    {
      std::lock_guard<std::mutex> lock(g_folder_mutex);
      folder.swap(g_folder);
    }
    if (!folder.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(folder, ec);
    }
  }
};

}  // namespace

void enable_run_exception_handler(bool jsonl)
{
  g_jsonl = jsonl;
  g_enabled = true;
}

void install_exception_handler_on_this_thread()
{
  if (!g_enabled) {
    return;
  }
  // The constructor of G4VExceptionHandler registers the object with this thread's state manager.
  static thread_local RunExceptionHandler* handler = new RunExceptionHandler();
  (void)handler;
}

void set_cleanup_folder(const std::string& path)
{
  std::lock_guard<std::mutex> lock(g_folder_mutex);
  g_folder = path;
}

void clear_cleanup_folder()
{
  std::lock_guard<std::mutex> lock(g_folder_mutex);
  g_folder.clear();
}
