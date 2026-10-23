#pragma once

#include <unistd.h>

#include <cstdio>
#include <iostream>

#include "G4UIsession.hh"

// Sends everything Geant4 prints (G4cout, G4cerr) to stderr. Geant4 writes to
// stdout by default, which would corrupt machine-readable stdout
// (docs/SIDECAR.md, section 2). Install it before the run manager is created,
// because the banner is printed on creation.
class StderrSession : public G4UIsession {
 public:
  G4int ReceiveG4cout(const G4String& text) override
  {
    std::cerr << text << std::flush;
    return 0;
  }
  G4int ReceiveG4cerr(const G4String& text) override
  {
    std::cerr << text << std::flush;
    return 0;
  }
};

// While alive, file descriptor 1 (stdout) is a copy of stderr. Some Geant4
// code writes to stdout without going through G4cout (seen with a cross-section
// table line), so StderrSession alone is not enough. Anything this program
// wants on stdout must be written after the guard is gone. POSIX only.
class StdoutToStderr {
 public:
  StdoutToStderr()
  {
    std::cout.flush();
    std::fflush(stdout);
    saved_ = ::dup(STDOUT_FILENO);
    if (saved_ >= 0) {
      ::dup2(STDERR_FILENO, STDOUT_FILENO);
    }
  }
  ~StdoutToStderr()
  {
    std::cout.flush();
    std::fflush(stdout);
    if (saved_ >= 0) {
      ::dup2(saved_, STDOUT_FILENO);
      ::close(saved_);
    }
  }
  StdoutToStderr(const StdoutToStderr&) = delete;
  StdoutToStderr& operator=(const StdoutToStderr&) = delete;

 private:
  int saved_ = -1;
};
