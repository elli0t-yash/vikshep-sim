#pragma once

#include <string>

// The private channel for machine-readable lines on stdout (docs/SIDECAR.md, section 2).
//
// capture() takes the real stdout for this channel and makes everything else harmless:
// file descriptor 1 is pointed at stderr, so printf, puts and anything a library writes to
// fd 1 goes to stderr, and std::cout is pointed at std::cerr's buffer. (Geant4 library
// code in worker threads writes to std::cout directly, bypassing G4cout: one stray line
// per worker thread, see STATUS.md.) Geant4's own G4cout/G4cerr are routed to stderr by
// StderrSession (stderr_session.hh).
//
// After capture(), the only way to put a line on the real stdout is write_line(), and
// all lines come out of this one function, one write() per line, under one lock, so
// lines never interleave. POSIX only.
class StdoutChannel {
 public:
  // Call once, early, before anything that may print. Also makes SIGPIPE harmless: if
  // the reader of stdout goes away, write_line() fails instead of killing the process.
  static void capture();

  static bool captured();

  // Writes `line` and a newline. `line` must be one JSON object without newlines. Does
  // nothing before capture(). If the write fails (the reader closed the pipe), this and
  // all later calls do nothing, broken() becomes true and a cancellation is requested
  // (nobody is listening any more).
  static void write_line(const std::string& line);

  static bool broken();
};
