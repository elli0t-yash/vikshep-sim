#include "commands.hh"

int main(int argc, char** argv)
{
  CLI::App app{"vikshep-sim: Geant4-based simulator driven by a JSON spec.\n\n"
               "Exit codes:\n"
               "  0  success\n"
               "  2  bad usage or invalid spec; also 'not implemented yet'"};
  app.require_subcommand(1);

  int exit_code = kExitOk;
  register_info(app, exit_code);
  register_validate(app, exit_code);
  register_dev_run(app, exit_code);
  register_run(app, exit_code);
  register_datasets(app, exit_code);

  try {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e) {
    // Usage errors exit 2 (docs/SIDECAR.md); --help exits 0.
    return app.exit(e) == 0 ? kExitOk : kExitUsageOrSpec;
  }
  return exit_code;
}
