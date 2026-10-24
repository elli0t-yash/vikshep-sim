"""Shared helpers of the Python tests."""
import os
import sys

REQUIRED_VARIABLES = ("G4LEDATA", "G4PARTICLEXSDATA", "G4ENSDFSTATEDATA", "G4LEVELGAMMADATA")


def require_data_sets():
    """Exits with code 77 (ctest: skipped) when the Geant4 data sets are missing, or with code 1
    if VIKSHEP_REQUIRE_DATA is set (CI): a skipped test must not let CI go green."""
    if all(os.path.isdir(os.environ.get(v, "")) for v in REQUIRED_VARIABLES):
        return
    must = os.environ.get("VIKSHEP_REQUIRE_DATA", "") not in ("", "0")
    sys.stderr.write("%s: Geant4 data sets are missing (environment not active?)\n" %
                     ("FAILED (VIKSHEP_REQUIRE_DATA is set)" if must else "SKIPPED, not passed"))
    sys.exit(1 if must else 77)
