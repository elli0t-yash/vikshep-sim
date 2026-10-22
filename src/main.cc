#include "commands.hh"

int main(int argc, char** argv)
{
  CLI::App app{"vikshep-sim: Geant4-based simulator driven by a JSON spec.\n\n"
               "Exit codes:\n"
               "  0  success\n"
               "  2  subcommand not implemented yet"};
  app.require_subcommand(1);

  int exit_code = kExitOk;
  register_info(app, exit_code);
  register_run(app, exit_code);
  register_datasets(app, exit_code);

  try {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e) {
    return app.exit(e);
  }
  return exit_code;
}
