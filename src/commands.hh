#pragma once

#include <CLI/CLI.hpp>

// Exit codes of vikshep-sim.
//   0  success
//   2  subcommand not implemented yet
constexpr int kExitOk = 0;
constexpr int kExitNotImplemented = 2;

// Each subcommand lives in its own file and registers itself on the app.
// The callback stores the process exit code in exit_code.
void register_info(CLI::App& app, int& exit_code);
void register_run(CLI::App& app, int& exit_code);
void register_datasets(CLI::App& app, int& exit_code);
