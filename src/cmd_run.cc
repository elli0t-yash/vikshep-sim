#include <iostream>

#include "commands.hh"

void register_run(CLI::App& app, int& exit_code)
{
  CLI::App* sub = app.add_subcommand(
      "run", "Run a simulation from a spec (not implemented; exits with code 2)");
  sub->footer("Exit code 2: not implemented (milestone Y3).");
  sub->callback([&exit_code]() {
    std::cerr << "not implemented (milestone Y3)\n";
    exit_code = kExitNotImplemented;
  });
}
