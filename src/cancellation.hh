#pragma once

// Cancellation by signal (docs/SIDECAR.md, section 7).
//
// The first SIGTERM or SIGINT only sets a flag (async-signal-safe); the run checks it
// and stops after the events in progress. A second signal ends the process at once with
// exit code 4 (`_exit`, async-signal-safe), without further output. Windows is
// specified (Ctrl-Break) but not built or tested.
namespace cancellation {

// Installs the handlers for SIGTERM and SIGINT.
void install_handlers();

// True once a first signal arrived, or request() was called.
bool requested();

// Requests cancellation from code (for example when stdout cannot be written any more).
void request();

// How many signals arrived (for tests of the handler).
int signals_received();

}  // namespace cancellation
