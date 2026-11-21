"""Builds fix-table rows by swapping one phrase inside a line, with the rest copied from the file.

WHY THIS EXISTS

The first attempt at the source tree typed whole replacement lines from a report that truncates its
display at ninety-two characters. Twenty lines lost the end of a sentence: "A winner cleared the"
became "A winner cle". Every one of those still cleared its finding. The gate went green on
twenty mutilated comments, and only a length comparison against the original found them.

So no replacement is typed here. A caller names a file, a line, the phrase to remove and the phrase
to put in its place. Everything around the phrase is read out of the file and copied verbatim into
both sides of the row, and that makes losing the tail of a line impossible instead of unlikely.

    python utils/maint/prose/build_fixes.py plan.tsv        # report the rows it would add
    python utils/maint/prose/build_fixes.py plan.tsv --write

A plan row is four fields: file, line number, exact phrase to remove, phrase to put in its place.
The phrase must appear on that line. The window taken around it grows until it is unique in the
file, and a phrase that cannot be made unique is reported instead of guessed at.
"""

import io
import os
import sys

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "prose_fixes.tsv")


def window(lines, number, phrase):
    """The smallest span of the line around `phrase` that appears once in the whole file.

    Grows outward a few characters at a time. A one-line comment repeated verbatim elsewhere in the
    file cannot be addressed this way and is reported instead of replaced at the wrong site.
    """
    line = lines[number - 1]
    at = line.find(phrase)
    if at < 0:
        return None, "phrase is not on that line"

    body = "\n".join(lines)
    for pad in range(0, 200, 8):
        start = max(0, at - pad)
        end = min(len(line), at + len(phrase) + pad)
        span = line[start:end]
        if body.count(span) == 1:
            return span, ""
    return None, "no unique window around the phrase"


def main():
    argv = [one for one in sys.argv[1:] if not one.startswith("-")]
    write = "--write" in sys.argv
    if not argv:
        sys.stdout.write(__doc__)
        return 2

    with io.open(argv[0], encoding="utf-8") as handle:
        plan = [one.rstrip("\n") for one in handle if one.strip() and not one.startswith("#")]

    held = {}
    rows = []
    bad = 0
    for entry in plan:
        parts = entry.split("\t")
        if len(parts) != 4:
            print("  MALFORMED  %r" % entry[:60])
            bad += 1
            continue
        name, number, phrase, into = parts
        number = int(number)
        path = os.path.join(HERE, name.replace("/", os.sep))
        if path not in held:
            with io.open(path, encoding="utf-8") as handle:
                held[path] = handle.read().split("\n")
        lines = held[path]

        span, why = window(lines, number, phrase)
        if span is None:
            print("  SKIP  %s:%d  %s" % (name, number, why))
            bad += 1
            continue

        # The only edit is the phrase. Everything else in the span came out of the file.
        fresh = span.replace(phrase, into)
        if fresh == span:
            print("  SKIP  %s:%d  the replacement changes nothing" % (name, number))
            bad += 1
            continue
        rows.append((name, span, fresh))

    if write and rows:
        with io.open(TABLE, "a", encoding="utf-8", newline="\n") as handle:
            for name, was, now in rows:
                handle.write("%s\t%s\t%s\n" % (name, was, now))

    for name, was, now in rows:
        print("  %-9s %s" % ("added" if write else "would add", name))
        print("      - %s" % was)
        print("      + %s" % now)

    print("")
    print("%d row(s) %s, %d skipped" % (len(rows), "added" if write else "to add", bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
