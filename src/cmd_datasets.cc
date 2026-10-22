#include <iostream>

#include "commands.hh"

void register_datasets(CLI::App& app, int& exit_code)
{
  CLI::App* sub = app.add_subcommand(
      "datasets", "Manage Geant4 data sets (not implemented; exits with code 2)");
  sub->footer("Exit code 2: not implemented (milestone Y6).");
  sub->callback([&exit_code]() {
    std::cerr << "not implemented (milestone Y6)\n";
    exit_code = kExitNotImplemented;
  });
}
