#!/usr/bin/env python3
"""Counts of the tests of a ctest run, as markdown for the CI job summary.

    python scripts/ci_summary.py ctest-junit.xml [job name] >> "$GITHUB_STEP_SUMMARY"

ctest writes the file with `ctest --output-junit`. The exit code is 1 if no test ran, if a test
failed, or if any test was skipped: CI must not go green by skipping.
"""
import sys
import xml.etree.ElementTree as ET


def main():
    path = sys.argv[1]
    job = sys.argv[2] if len(sys.argv) > 2 else ""
    root = ET.parse(path).getroot()
    cases = list(root.iter("testcase"))
    passed, failed, skipped = [], [], []
    for case in cases:
        name = case.get("name", "?")
        children = {child.tag for child in case}
        if "failure" in children or "error" in children or case.get("status") == "fail":
            failed.append(name)
        elif "skipped" in children or case.get("status") == "notrun":
            skipped.append(name)
        else:
            passed.append(name)

    print("### Tests%s" % (" (%s)" % job if job else ""))
    print()
    print("| ran | passed | failed | skipped |")
    print("|----:|-------:|-------:|--------:|")
    print("| %d | %d | %d | %d |" % (len(cases), len(passed), len(failed), len(skipped)))
    print()
    if failed:
        print("Failed: " + ", ".join("`%s`" % n for n in failed))
    if skipped:
        print("Skipped (not allowed in CI): " + ", ".join("`%s`" % n for n in skipped))
    if not cases:
        print("No test ran.")
    ok = bool(cases) and not failed and not skipped
    sys.stderr.write("tests: ran %d, passed %d, failed %d, skipped %d\n" % (len(cases), len(passed), len(failed), len(skipped)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
