"""Proposes a phrase swap for each finding, for build_fixes.py to turn into table rows.

The gate names a token instead of a phrase: it reports the bare contrast word where the text carries its two-word form. A swap
has to act on the phrase. Each site is widened to the construction actually present and then a
rule is applied to that construction.

    python utils/maint/prose/plan_fixes.py src hooks > plan.tsv
    python utils/maint/prose/build_fixes.py plan.tsv --write
    python utils/maint/prose/fix_prose.py --write

Sites with no rule are printed to stderr and left for a person. That is the intended split: the
mechanical majority is one substitution with the sentence otherwise untouched, and everything else
needs the sentence read.

WHAT A RULE MAY DO

Swap a connective for one that carries the same relation. The two forms in the table below mark a
contrast between two parallel things, and where the two sides are parallel the swap says the same
thing. Where they are not, the result reads wrong, and for that reason every proposal goes through the
gate before it is written and every applied line is length-checked against the original afterward.
"""

import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SHOW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "show_findings.py")

# Widen a reported token to the construction it belongs to, longest first.
# The comma-carrying form is tried first. Replacing a bare explainer with its comma-led form where the
# source already ends the previous clause with a comma produces `, , the`. The comma the source
# owns has to be inside the construction being swapped, never added by the replacement.
WIDER = {
    "rather": ["rather than"],
    "so a": ["so a"],
    "which is the": [", which is the whole of", ", which is the whole", ", which is the",
                     "which is the whole of", "which is the whole", "which is the"],
    "which is what": [", which is what makes", ", which is what",
                      "which is what makes", "which is what"],
    "which is why": [", which is why", "which is why"],
    "is what makes": ["is what makes"],
    "That is what": ["That is what makes", "That is what"],
    "that is what": ["that is what makes", "that is what"],
    "and nothing else": [" and nothing else", "and nothing else"],
    "and nothing more": [" and nothing more", "and nothing more"],
    "on purpose": ["on purpose"],
    "is exactly the": ["is exactly the"],
    "is exactly what": ["is exactly what"],
    "is the one": ["is the one"],
    "The one place": ["The one place"],
    "The one thing": ["The one thing"],
    "the one thing": ["the one thing"],
    "That is the reason": ["That is the reason"],
    "That is why": ["That is why"],
    "and that is why": ["and that is why"],
}

# construction -> replacement. Only relations that survive the swap.
RULE = {
    "rather than": "and not",
    ", which is the whole of": ", all of",
    ", which is the whole": ", the entirety of",
    ", which is the": ", the",
    ", which is what makes": ", and that leaves",
    ", which is what": ", as",
    ", which is why": ", and so",
    "which is the whole of": "all of",
    "which is the whole": "the entirety of",
    "which is the": "the",
    "which is what makes": "and that leaves",
    "which is what": "as",
    "which is why": "and so",
    "That is what makes": "That leaves",
    "that is what makes": "that leaves",
    " and nothing else": ", alone",
    " and nothing more": ", alone",
    "and nothing else": ", alone",
    "and nothing more": ", alone",
    "on purpose": "deliberately",
    "is exactly the": "is precisely the",
    "is exactly what": "is precisely what",
    "The one place": "The single place",
    "The one thing": "The single thing",
    "the one thing": "the single thing",
    "That is the reason": "That is the ground on which",
    "That is why": "For that reason",
    "and that is why": ", and for that reason",
}


def sites(targets):
    done = subprocess.run([sys.executable, SHOW, "--tsv"] + list(targets),
                          capture_output=True, text=True, encoding="utf-8", env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    out = []
    for line in done.stdout.replace("\r", "").split("\n"):
        parts = line.split("\t")
        if len(parts) == 4:
            out.append((parts[0], int(parts[1]), parts[2], parts[3]))
    return out


def main():
    targets = [one for one in sys.argv[1:] if not one.startswith("-")] or ["src"]
    seen = set()
    planned = 0
    left = 0

    for name, number, token, text in sites(targets):
        if (name, number, token) in seen:
            continue
        seen.add((name, number, token))

        phrase = None
        for candidate in WIDER.get(token, [token]):
            if candidate in text:
                phrase = candidate
                break

        # The contrast phrase takes two shapes. Before a noun or a participle it survives as
        # the paired form, where a null described one way becomes one described the other. Before a gerund
        # it does not, because the verb needs a preposition to hang from. A deferral located before
        # a gerund takes the preposition form, and the paired form reads wrong in that position.
        if phrase == "rather than":
            after = text.split("rather than", 1)[1].strip().split(" ")
            following = after[0] if after else ""
            if following.endswith("ing") and following not in ("nothing", "everything", "string"):
                print("%s\t%d\t%s\t%s" % (name, number, phrase, "instead of"))
                planned += 1
                continue

        if phrase is None or phrase not in RULE:
            sys.stderr.write("no rule: %s:%d [%s]\n    %s\n" % (name, number, token, text.strip()))
            left += 1
            continue

        print("%s\t%d\t%s\t%s" % (name, number, phrase, RULE[phrase]))
        planned += 1

    sys.stderr.write("\n%d planned, %d left for a person\n" % (planned, left))
    return 0


if __name__ == "__main__":
    sys.exit(main())
