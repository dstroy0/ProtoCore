#!/usr/bin/env python3
"""Add words (argv) to cspell.json's "words" list: case-insensitive dedup, keep the list sorted if
it already was, preserve the file's 2-space indentation. Used by the pre-commit hook so unknown
technical terms in the committed docs never fail the CI spellcheck."""

import json
import sys

CFG = "cspell.json"


def main() -> int:
    words_in = [w for w in sys.argv[1:] if w]
    if not words_in:
        return 0
    with open(CFG, encoding="utf-8") as f:
        cfg = json.load(f)
    words = cfg.get("words", [])
    have = {w.lower() for w in words}
    was_sorted = words == sorted(words, key=str.lower)
    added = []
    for w in words_in:
        if w.lower() not in have:
            words.append(w)
            have.add(w.lower())
            added.append(w)
    if not added:
        return 0
    if was_sorted:
        words.sort(key=str.lower)
    cfg["words"] = words
    with open(CFG, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("cspell: added %d word(s): %s" % (len(added), ", ".join(added)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
