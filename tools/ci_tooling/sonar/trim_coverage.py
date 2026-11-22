"""Fits the committed coverage report to the sources of the checkout before the SonarCloud scan.

The generic coverage sensor of SonarCloud refuses the whole report when one line it names is past the end of its
file. The report comes from the last whole-matrix run, so a commit that shortens a covered file puts lines past
its end until the next run. This drops each such line, and each file that the checkout does not hold.

Usage: python tools/ci_tooling/sonar/trim_coverage.py [report]
"""

import pathlib
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[3]


def trim(report: pathlib.Path) -> tuple[int, int]:
    """Drop the lines past each end and the files that are gone. Returns the count of each."""
    tree = ET.parse(report)
    coverage = tree.getroot()
    lines_dropped = 0
    files_dropped = 0
    for node in list(coverage.iter("file")):
        source = ROOT / node.get("path", "")
        if not source.is_file():
            coverage.remove(node)
            files_dropped += 1
            continue
        count = len(source.read_bytes().splitlines())
        for line in list(node.iter("lineToCover")):
            if int(line.get("lineNumber", "0")) > count:
                node.remove(line)
                lines_dropped += 1
    tree.write(report, encoding="utf-8", xml_declaration=True)
    return lines_dropped, files_dropped


def main() -> int:
    report = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "test/coverage.xml"
    if not report.is_file():
        print(f"trim_coverage: no report at {report}")
        return 0
    lines_dropped, files_dropped = trim(report)
    print(f"trim_coverage: {lines_dropped} lines past the end of their file, {files_dropped} files gone")
    return 0


if __name__ == "__main__":
    sys.exit(main())
