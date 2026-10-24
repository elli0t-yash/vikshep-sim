#pragma once

#include <nlohmann/json.hpp>

// The facts about this build that `info --json` and provenance.json both report.

// {"arch": ..., "compiler": ..., "os": ...}
nlohmann::json platform_json();

// {"gdml": ..., "multithreaded": ..., "version": ..., "version_number": ...}
nlohmann::json geant4_json();
