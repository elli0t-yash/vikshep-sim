#pragma once

#include <CLI/CLI.hpp>

// Exit codes of vikshep-sim.
//   0  success
//   1  simulation or internal error
//   2  bad usage or invalid spec (docs/SIDECAR.md); also what the
//      not-yet-implemented subcommands return
constexpr int kExitOk = 0;
constexpr int kExitUsageOrSpec = 2;
constexpr int kExitNotImplemented = 2;
constexpr int kExitFailure = 1;
constexpr int kExitDataSetsMissing = 3;

// Each subcommand lives in its own file and registers itself on the app.
// The callback stores the process exit code in exit_code.
void register_info(CLI::App& app, int& exit_code);
void register_validate(CLI::App& app, int& exit_code);
void register_run(CLI::App& app, int& exit_code);
void register_datasets(CLI::App& app, int& exit_code);
