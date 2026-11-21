"""Grade the repair table itself, because nothing else does.

`prose_fixes.tsv` is a find-and-replace table applied to source files. Two faults in it are
invisible everywhere else, and one of each was live when this was written.

FAULT ONE: A REPLACEMENT THAT IS ITSELF BANNED

Line 20 replaced `is exactly the` with `is precisely the`. Both are banned by `docs_check` - the
first at its line 63, the second at line 85 - so the rule carried a banned phrase from one form
to another and reported success. It survived because its target is a `.h`, and the docs gate blanks
code. The file the rule edits is a file the gate does not read. A repair table that can introduce
the thing it repairs is worse than no table, because the run comes back clean.

FAULT TWO: A REPLACEMENT THAT DELETES A WORD AND LEAVES A HOLE

A rule whose replacement drops a word can leave a sentence ungrammatical, and NO gate can see it: a
broken sentence still renders, contains no banned token, and is therefore neither breaking nor
prose. Found in the tree, all from this shape:

    `is the one to take seriously`  ->  `is the to take seriously`
    "what remains after every"      ->  "what remains every"
    "the arm whose arithmetic"      ->  "the whose arithmetic"

The mechanism is always the same. Text was written with a banned token inside it, a rule removed the
token, and the sentence around it was never re-read. The repair cannot be to restore the word, since
that restores the ban; it has to be a rewrite, which a table cannot do.

So deletions are not banned here - sometimes dropping a word IS the correct repair - they are
REPORTED, every one, for a human to read the resulting sentence. The check is that someone looked,
not that the rule is forbidden.

    python utils/maint/prose/check_fixes.py
"""

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TABLE = os.path.join(HERE, "prose_fixes.tsv")

sys.path.insert(0, os.path.join(ROOT, "utils", "maint", "prose"))
import docs_check


def rows_of(path):
    """Every rule as (line number, target file, find, replace). Blank and comment lines skipped."""
    out = []
    with io.open(path, "r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                out.append((number, None, None, None))
                continue
            out.append((number, parts[0], parts[1], parts[2]))
    return out


def banned_in(text):
    """Every banned pattern this text matches, by the same list the docs gate uses.

    Read from `docs_check.BANNED` instead of copied. The two cannot drift apart. A second copy
    of a ban list is the same defect this file exists to catch, one level up.
    """
    hits = []
    for pattern in docs_check.BANNED:
        if re.search(pattern, text, re.IGNORECASE):
            hits.append(pattern)
    return hits


def main():
    rows = rows_of(TABLE)
    broken = 0
    deletions = []

    print("=" * 78)
    print("  GRADING THE REPAIR TABLE")
    print("=" * 78)
    print()
    print("  %d rules in %s" % (len(rows), os.path.relpath(TABLE, ROOT).replace("\\", "/")))
    print()

    for number, target, find, replace in rows:
        if target is None:
            print("  BREAK line %d: not three tab-separated fields" % number)
            broken += 1
            continue

        # THE REPLACEMENT MUST BE CLEAN. The thing being written into the file is graded by the same
        # list that would reject it if a human typed it there.
        hits = banned_in(replace)
        if hits:
            print("  BREAK line %d: the REPLACEMENT is itself banned" % number)
            print("      file     %s" % target)
            print("      find     %r" % find)
            print("      replace  %r" % replace)
            for pattern in hits:
                print("      matches  %s" % pattern)
            broken += 1
            continue

        # A rule that does not shorten cannot leave a hole. Only the shrinking ones are listed.
        if len(replace.split()) < len(find.split()):
            deletions.append((number, target, find, replace))

    print()
    print("-" * 78)
    print()
    print("  %d rule(s) drop at least one word. Each one can leave a sentence ungrammatical," % len(deletions))
    print("  and no gate in this tree can see that, and each has to be read by a human:")
    print()
    for number, target, find, replace in deletions[:40]:
        short_find = find if len(find) <= 40 else find[:37] + "..."
        short_replace = replace if len(replace) <= 40 else replace[:37] + "..."
        print("    %3d  %-42r -> %r" % (number, short_find, short_replace))
    if len(deletions) > 40:
        print("    ... and %d more" % (len(deletions) - 40))

    print()
    print("=" * 78)
    if broken:
        print("  %d rule(s) would write a banned phrase. Fix them before running the table." % broken)
    else:
        print("  No rule writes a banned phrase.")
    print("=" * 78)
    return 1 if broken else 0


if __name__ == "__main__":
    raise SystemExit(main())
