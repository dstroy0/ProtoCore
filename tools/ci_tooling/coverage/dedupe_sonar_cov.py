#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""dedupe_sonar_cov.py - collapse repeated lines in a SonarQube generic coverage report.

gcovr runs with --merge-mode-functions=separate so the two #if/#else definitions of one function
each keep the coverage their own envs measured. The same flag emits a header's inline lines once per
translation unit that included it, and SonarQube's generic format requires each line to appear once
per file - the whole report is rejected with "Error during parsing" when one does not.

This unions the repeats: a line is covered when any entry covered it, and the branch counts take the
highest pair seen. Function-level separation is untouched; only the per-line records the format
demands be unique are folded.

    python3 -m tools.ci_tooling.coverage.dedupe_sonar_cov test/coverage.xml

Idempotent, so it is safe to run over an already-clean report.
"""

import os
import sys
import xml.etree.ElementTree as ET


def dedupe(path):
    tree = ET.parse(path)
    root = tree.getroot()
    folded = 0

    for f in root.findall("file"):
        best = {}
        order = []
        for ltc in f.findall("lineToCover"):
            num = int(ltc.get("lineNumber"))
            covered = ltc.get("covered") == "true"
            btc = int(ltc.get("branchesToCover") or 0)
            cb = int(ltc.get("coveredBranches") or 0)
            if num not in best:
                best[num] = [covered, btc, cb]
                order.append(num)
            else:
                cur = best[num]
                cur[0] = cur[0] or covered
                cur[1] = max(cur[1], btc)
                cur[2] = max(cur[2], cb)
                folded += 1
            f.remove(ltc)

        for num in order:
            covered, btc, cb = best[num]
            el = ET.SubElement(f, "lineToCover")
            el.set("lineNumber", str(num))
            el.set("covered", "true" if covered else "false")
            if btc:
                el.set("branchesToCover", str(btc))
                el.set("coveredBranches", str(cb))

    tmp = path + ".tmp"
    tree.write(tmp, encoding="utf-8", xml_declaration=True)
    os.replace(tmp, path)
    return folded


def main(argv):
    if len(argv) != 1:
        print("usage: dedupe_sonar_cov.py <coverage.xml>", file=sys.stderr)
        return 2
    folded = dedupe(argv[0])
    print("dedupe_sonar_cov: folded %d repeated line record(s) in %s" % (folded, argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
