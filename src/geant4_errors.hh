#pragma once

#include <string>

// Geant4 reports serious problems through G4Exception. Its default handler prints a banner and
// calls abort(), which kills the process with a core dump and without any JSON line. For the
// `run` command (docs/SIDECAR.md, section 4) a fatal Geant4 exception must instead become one
// error line with code SIMULATION_FAILED, no output folder, and exit code 1.

// Turns the handler on for this process. `run` calls it first thing; other commands never do,
// so they keep Geant4's own behaviour. `jsonl` says whether the error line is written to the
// stdout channel (--progress jsonl).
void enable_run_exception_handler(bool jsonl);

// Installs the handler on the calling thread (Geant4 keeps the handler per thread: call it on the
// master thread and on every worker thread). Does nothing unless enabled; safe to call again.
void install_exception_handler_on_this_thread();

// The temporary output folder to delete if the process has to stop at once. Set by the pipeline
// when it creates the folder, cleared when the folder is gone or in place.
void set_cleanup_folder(const std::string& path);
void clear_cleanup_folder();
