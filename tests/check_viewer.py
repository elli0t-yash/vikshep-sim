#!/usr/bin/env python3
"""Runs tests/check_viewer_core.mjs (the replay logic and static checks of scripts/view_3d.html) with Node.

Usage: check_viewer.py <repo dir>
Exit code 77 (ctest: skipped) when node is missing; a failure when VIKSHEP_REQUIRE_DATA is set.
"""
import os
import shutil
import subprocess
import sys

repo = sys.argv[1]
node = shutil.which("node")
if node is None:
    print("node not found")
    sys.exit(1 if os.environ.get("VIKSHEP_REQUIRE_DATA") else 77)
sys.exit(subprocess.run([node, os.path.join(repo, "tests", "check_viewer_core.mjs"),
                         os.path.join(repo, "scripts", "view_3d.html"), os.path.join(repo, "tests", "fixtures")]).returncode)
