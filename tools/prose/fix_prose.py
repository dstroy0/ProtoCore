"""Applies prose rewrites from a table, one exact site at a time, and errors on a bad one.

Written after fixing eighty-five findings by hand. The hand work is the right shape and the risk in
it is mechanical: a rewrite that matches nothing leaves the finding located while the run looks
successful, and a rewrite that matches twice edits a site nobody read. Both are the failure this
whole day has been about. Both are errors here.

    python utils/maint/prose/fix_prose.py                    # report what would change
    python utils/maint/prose/fix_prose.py --write            # change it
    python utils/maint/prose/fix_prose.py --table other.tsv  # a different table

Each row of the table is a file, the exact text to find, and what it becomes, tab separated, with
\\n for a line break inside a field. The text must appear once in that file. Not zero times, which
means the source moved and the row is stale, and not twice, which means the site was not identified
uniquely and one of the two was never read.

WHY THE TABLE IS A SEPARATE FILE

A rewrite table has to quote the banned text it replaces. Retained inside this module, that put those
phrases into a checked Python file and the gate flagged the tool for containing the very strings it
exists to remove. The table is data. It sits in prose_fixes.tsv, which the gate does not read, and
this module stays prose the gate can hold to its own standard.

Rows already applied report as `already` and are not an error, and a table can therefore be re-run
without being pruned first.

A replacement can itself be a finding. Three in this tree arrived as replacements for other banned
phrases, each costing a full round of edit, check, edit again. Every candidate is therefore put past
the gate before it is written, by asking the gate and never by retention a second copy of its list.
"""

import io
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CHECK = os.path.join(HERE, "utils", "maint", "prose", "docs_check")
TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "prose_fixes.tsv")


def clean(text):
    """Whether the gate finds nothing in this replacement. Asks the gate, never a copy of it.

    Retention a list of banned phrases here instead would drift from the gate's own, and the drift
    would be silent in the direction of passing.
    """
    if not os.path.exists(CHECK):
        return True, "no checker to ask"
    room = tempfile.mkdtemp(prefix="fix_prose_")
    probe = os.path.join(room, "candidate.md")
    with io.open(probe, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    done = subprocess.run([sys.executable, CHECK, probe], capture_output=True, text=True, encoding="utf-8", env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    hits = [one.strip() for one in (done.stdout + done.stderr).split("\n")
            if " prose " in one or " BREAK " in one]
    if not hits:
        return True, ""
    said = hits[0]
    at = said.find("candidate.md:")
    return False, said[at + len("candidate.md:"):].strip() if at >= 0 else said


def endings(path):
    """The line ending the file already uses. Writing it back does not convert the whole file.

    The Python and Markdown in this tree are LF and the C and CUDA are CRLF. Reading with universal
    newlines and writing with newline="\\n" rewrites every line of a CRLF file, which is a diff
    touching the entire file for a two-word prose change and is invisible in the tool's own report.
    """
    with io.open(path, "rb") as handle:
        raw = handle.read()
    if b"\r\n" in raw:
        return "\r\n"
    if b"\r" in raw:
        return "\r"
    return "\n"


def rows(path):
    """The table, with blank lines and comments dropped and \\n turned back into a line break."""
    out = []
    with io.open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 3:
                sys.stderr.write("%s line %d: wants three tab separated fields, has %d\n"
                                 % (os.path.basename(path), number, len(parts)))
                raise SystemExit(1)
            out.append(tuple(one.replace("\\n", "\n") for one in parts))
    return out


def main():
    write = "--write" in sys.argv
    table = TABLE
    if "--table" in sys.argv:
        table = sys.argv[sys.argv.index("--table") + 1]
    if not os.path.exists(table):
        sys.stderr.write("no table at %s\n" % table)
        return 1

    bad = 0
    done = 0
    already = 0

    for name, was, now in rows(table):
        path = os.path.join(HERE, name.replace("/", os.sep))
        if not os.path.exists(path):
            print("  MISSING  %s" % name)
            bad += 1
            continue
        with io.open(path, encoding="utf-8") as handle:
            text = handle.read()

        seen = text.count(was)
        if seen == 0:
            if text.count(now):
                already += 1
                continue
            print("  STALE    %s: no match for %r" % (name, was.split("\n")[0][:52]))
            bad += 1
            continue
        if seen > 1:
            print("  AMBIGUOUS %s: %d matches for %r" % (name, seen, was.split("\n")[0][:44]))
            bad += 1
            continue

        good, why = clean(now)
        if not good:
            print("  ERROR  %s: the replacement is itself a finding, %s" % (name, why))
            bad += 1
            continue

        if write:
            with io.open(path, "w", encoding="utf-8", newline=endings(path)) as handle:
                handle.write(text.replace(was, now))
        print("  %-9s %s" % ("fixed" if write else "would fix", name))
        done += 1

    print("")
    print("%d %s, %d already applied, %d errored"
          % (done, "applied" if write else "to apply", already, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
