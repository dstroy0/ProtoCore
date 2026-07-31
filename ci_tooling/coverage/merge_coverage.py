#!/usr/bin/env python3
"""Union per-env SonarQube generic-coverage reports into one, with optional baseline overlay.

gcov cannot merge the same source compiled with different flags across envs in a
single pass (it raises a worker exception), so gen_coverage.sh emits one report
per env and this unions them: a line is covered if it is covered in ANY env, and
the per-line branch counts from the env that covered the most branches are kept.
Output is a single SonarQube generic-coverage report (one <file> per path).

Incremental (affected-only) CI runs pass --baseline: only the envs affected by the
diff run, so the fresh reports cover only some files. The committed coverage.xml is
overlaid so the output is still whole-project.

Which rule applies to a file depends on whether THIS run re-measured all of it. Pass
--dep-graph/--ran-envs and the decision is made per file from the compiler include
graph (test/dep_graph.json: path -> the envs that compile or include it):

  * every env that owns the file ran  -> AUTHORITATIVE: the fresh view REPLACES the
    baseline outright, and if the file produced no coverage at all this run its
    baseline record is DROPPED (it was deleted or renamed);
  * only some of its envs ran         -> UNION with the baseline (this run exercised
    only part of its callers, so replacing would wrongly lower it);
  * not recompiled at all             -> baseline kept verbatim.

Union is monotonic - it can only ever raise a line - so a file that is never made
authoritative can never lose a stale entry. That is how the committed baseline drifts:
before this, ONLY the changed .cpp paths were replaceable, so a coverage record could
outlive the code it described (headers were excluded outright, and deleted files were
immortal). --changed is still honoured as an extra force-replace set.

Usage:
    merge_coverage.py <out.xml> <report-glob>
    merge_coverage.py <out.xml> <report-glob> --baseline coverage.xml \
        --dep-graph test/dep_graph.json --ran-envs "native_a native_b"
"""

import argparse
import glob
import json
import xml.etree.ElementTree as ET


def parse_report(path, into):
    """Union one SonarQube report at `path` into the {path: {line: [cov,btc,cb]}} dict `into`."""
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, FileNotFoundError):
        return False
    for f in root.findall("file"):
        _union_file(into.setdefault(f.get("path"), {}), f)
    return True


def _union_line(lines, ln, cov, btc, cb):
    if ln not in lines:
        lines[ln] = [cov, btc, cb]
    else:
        cur = lines[ln]
        cur[0] = cur[0] or cov
        # Keep the branch pair from whichever env covered the most branches. This is the best that
        # can be done from the SonarQube generic format, which carries only the per-line AGGREGATE
        # (branchesToCover, coveredBranches) - it does not say WHICH branches were taken, so two envs
        # that each cover a different subset cannot be combined. Pass --json-reports to union
        # per-branch from gcovr's JSON instead; see per_branch_union().
        if (cb if cb is not None else -1) > (cur[2] if cur[2] is not None else -1):
            cur[1], cur[2] = btc, cb


def per_branch_union(pattern):
    """{path: {line: (covered, n_branches, n_covered_branches)}} unioned BRANCH BY BRANCH.

    The XML path above can only keep the single best env per line, so a condition whose branches are
    split across envs - one env taking the true arm, another the false arm - is reported as partially
    covered forever, even though the suite as a whole covers every branch. gcovr's JSON keeps the
    per-branch counts, so here a branch is covered if ANY env took it.

    Envs compile with different -D flags, so the same line can legitimately have a different number
    of branches in different envs. Branch indices are only comparable within one shape, so lines are
    unioned per (line, branch-count) and the widest shape seen for a line wins - that is the build
    that actually had the most conditions to cover.

    GCOVR_EXCL_* markers are honoured here the same way gcovr honours them in the SonarQube report
    it writes alongside this JSON: gcovr drops an excluded line/branch from the XML but still emits
    it in the JSON tagged "gcovr/excluded". Feeding those back in would resurrect every house
    exclusion in src/ (and they would then be reported as permanently uncovered, since by
    construction nothing can execute them).
    """
    acc = {}  # path -> line -> {n_branches: [covered_flags]}
    hits = {}  # path -> line -> line-level covered
    for rep in sorted(glob.glob(pattern)):
        try:
            with open(rep, encoding="utf-8") as fh:
                doc = json.load(fh)
        except (OSError, ValueError):
            continue
        for f in doc.get("files", []):
            path = f.get("file", "").replace("\\", "/")
            if not path:
                continue
            pl = acc.setdefault(path, {})
            ph = hits.setdefault(path, {})
            for l in f.get("lines", []):
                ln = l.get("line_number")
                if not isinstance(ln, int) or ln < 1:
                    continue
                if l.get("gcovr/excluded"):
                    continue  # GCOVR_EXCL_* - gcovr already dropped it from the XML report
                ph[ln] = ph.get(ln, False) or bool(l.get("count"))
                brs = [b for b in (l.get("branches") or []) if not b.get("gcovr/excluded")]
                if not brs:
                    continue
                shapes = pl.setdefault(ln, {})
                flags = shapes.setdefault(len(brs), [False] * len(brs))
                for i, b in enumerate(brs):
                    if b.get("count"):
                        flags[i] = True

    out = {}
    for path, lines in acc.items():
        rec = {}
        for ln, covered in hits[path].items():
            shapes = lines.get(ln)
            if not shapes:
                rec[ln] = (covered, None, None)
            else:
                width = max(shapes)  # the build with the most conditions on this line
                flags = shapes[width]
                rec[ln] = (covered, width, sum(1 for x in flags if x))
        out[path] = rec
    return out


def _union_file(lines, file_el):
    for lc in file_el.findall("lineToCover"):
        try:
            ln = int(lc.get("lineNumber"))
        except (TypeError, ValueError):
            continue  # malformed entry (no / non-integer lineNumber): skip
        if ln < 1:
            continue  # Sonar requires lineNumber >= 1; a stray line-0 entry would break the whole report
        cov = lc.get("covered") == "true"
        btc = lc.get("branchesToCover")
        cb = lc.get("coveredBranches")
        _union_line(lines, ln, cov, int(btc) if btc is not None else None, int(cb) if cb is not None else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?", default="coverage.xml")
    ap.add_argument("reports", nargs="?", default="coverage_reports/*.xml", help="glob of per-env reports")
    ap.add_argument("--baseline", help="committed coverage.xml to overlay under the fresh reports")
    ap.add_argument("--changed", help="file with one changed path per line; these are replaced, not unioned")
    ap.add_argument("--dep-graph", help="test/dep_graph.json (path -> owning envs), for replace-vs-union")
    ap.add_argument("--ran-envs", help="space/newline separated envs that ran to completion this run")
    ap.add_argument("--json-reports", help="glob of gcovr JSON reports; unions BRANCH BY BRANCH "
                                           "instead of keeping one env's aggregate per line")
    args = ap.parse_args()

    # 1) Union the fresh per-env reports (this run's actual coverage).
    files = {}
    nrep = 0
    for rep in sorted(glob.glob(args.reports)):
        if parse_report(rep, files):
            nrep += 1

    # 1b) If gcovr JSON is available, it supersedes the per-line aggregates above: it can tell which
    # branches were taken, so a condition split across envs adds up instead of being capped at the
    # best single env.
    if args.json_reports:
        pb = per_branch_union(args.json_reports)
        upgraded = 0
        for path, rec in pb.items():
            dst = files.setdefault(path, {})
            for ln, (cov, btc, cb) in rec.items():
                prev = dst.get(ln)
                if prev and prev[2] is not None and cb is not None and cb > prev[2]:
                    upgraded += 1
                dst[ln] = [cov, btc, cb]
        if upgraded:
            print(f"  per-branch union recovered {upgraded} line(s) the per-line merge under-counted")

    # 2) Overlay the baseline so the report stays whole-project on a partial run.
    if args.baseline:
        changed = set()
        if args.changed:
            try:
                with open(args.changed, encoding="utf-8") as fh:
                    changed = {ln.strip().replace("\\", "/") for ln in fh if ln.strip()}
            except FileNotFoundError:
                pass
        # A file is AUTHORITATIVE when every env that owns it (per the compiler include graph) ran
        # to completion here: this run saw all of its callers, so the fresh view is the whole truth
        # and the baseline must not be allowed to add to it. Without this only --changed paths were
        # ever replaceable, and union-only merging made every other stale line immortal.
        owners = {}
        ran = set()
        if args.dep_graph and args.ran_envs:
            try:
                with open(args.dep_graph, encoding="utf-8") as fh:
                    owners = {k.replace("\\", "/"): set(v) for k, v in json.load(fh).items()}
            except (OSError, ValueError) as exc:
                print(f"  warning: --dep-graph unusable ({exc}); falling back to union-only")
            ran = set(args.ran_envs.split())

        def authoritative(path):
            own = owners.get(path)
            # Unknown to the graph (brand-new file, or no graph) -> not authoritative: union is the
            # safe direction, since replacing on partial data would wrongly lower a file.
            return bool(own) and own <= ran

        base = {}
        parse_report(args.baseline, base)
        dropped_stale = []
        for path, lines in base.items():
            if path in changed or authoritative(path):
                continue  # fully re-measured (or explicitly changed): keep only the fresh view
            if path not in files:
                files[path] = lines  # untouched file: keep the baseline verbatim
            else:
                for ln, val in lines.items():  # partially re-measured: union (never lower)
                    _union_line(files[path], ln, val[0], val[1], val[2])

        # A file whose every env ran but which produced no coverage at all is gone (deleted or
        # renamed). Union-only merging kept such records forever; drop them.
        for path in base:
            if path not in files and authoritative(path):
                dropped_stale.append(path)
        if dropped_stale:
            print(f"  dropped {len(dropped_stale)} stale baseline file(s) no longer producing "
                  f"coverage: {', '.join(sorted(dropped_stale)[:10])}")

    # Emit only Sonar-valid <file> elements. The generic-coverage parser rejects a file with no
    # <lineToCover>, an empty path, a lineNumber < 1, or coveredBranches > branchesToCover; a single
    # such entry (e.g. a line-0 artefact from a differing gcov version) makes the WHOLE report
    # unparseable ("Error during parsing of the generic coverage report"). Drop the offenders here so a
    # tool quirk degrades one line instead of zeroing all coverage, and name them so CI stays diagnosable.
    cov_el = ET.Element("coverage", {"version": "1"})
    dropped = []
    for path in sorted(files):
        if not path or not files[path]:
            dropped.append(path or "<no-path>")
            continue
        fe = ET.SubElement(cov_el, "file", {"path": path})
        for ln in sorted(files[path]):
            cov, btc, cb = files[path][ln]
            attrs = {"lineNumber": str(ln), "covered": "true" if cov else "false"}
            if btc is not None:
                cov_b = cb if cb is not None else 0
                if cov_b > btc:  # never report more covered branches than exist
                    cov_b = btc
                attrs["branchesToCover"] = str(btc)
                attrs["coveredBranches"] = str(cov_b)
            ET.SubElement(fe, "lineToCover", attrs)
    ET.ElementTree(cov_el).write(args.out, encoding="utf-8", xml_declaration=True)
    tag = f" (+baseline {args.baseline})" if args.baseline else ""
    emitted = len(files) - len(dropped)
    print(f"merged {nrep} reports{tag} -> {args.out}: {emitted} files, {sum(len(v) for v in files.values())} lines")
    if dropped:
        print(f"  dropped {len(dropped)} empty/invalid <file> entries: {', '.join(dropped[:10])}")


if __name__ == "__main__":
    main()
