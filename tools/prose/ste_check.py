#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""The ASD-STE100 check: every technical document checked against the Simplified Technical English writing rules.

One file, the same in orior, MMgr and embedded_types; orior's harness holds the three copies byte for byte.

    ste_check.py [paths]                    the findings in every technical document under the paths
    ste_check.py --ratchet FILE [paths]     fail where a document carries more findings than FILE allows it
    ste_check.py --ratchet-write FILE ...   write each document's count into FILE, lowered and never raised
    ste_check.py --words FILE ...           the approved word lists, one word a line or a word in a TSV's first
                                            column; a word outside every list is a finding

A technical document is a Markdown file of the tree's prose: the README, the contributing and security notes and
everything under docs/, but not in a folder that is a repository of its own. A code block, an inline span of code, a table, a heading, a link target, an HTML comment
and a license section are not prose and are not read.

The rules checked, with their numbers in ASD-STE100 Issue 9:

  sentence     a procedural sentence, a numbered step, has 20 words at max (Rule 5.1); a descriptive sentence
               25 at max (Rule 6.2)
  paragraph    a descriptive paragraph has 6 sentences at max (Rule 6.6)
  ing          no -ing form of a verb, but in a technical name (Rule 3.6)
  passive      the active voice in every sentence (Rules 3.7 and 3.8): a form of `be` before a past participle
  tense        the simple tenses only (Rule 3.4): no `have` or `has` before a past participle, no `be` before an -ing
  word         every word is an approved word or a technical name (Rules 1.1 and 1.5), where an approved list is
               given; a technical name is a word in code, a capitalized word or one with a digit or an underscore

The words a rule allows are technical names to that rule, and each repository lists them in ste_names.txt beside
this file and its ratchet: a name like `string` is a technical name, which Rule 3.6 lets retain its -ing.
"""

import os
import re
import sys

LIMIT_PROCEDURAL = 20
LIMIT_DESCRIPTIVE = 25
LIMIT_PARAGRAPH = 6

DOCUMENT_NAMES = ("README.md", "CONTRIBUTING.md", "SECURITY.md")
DOCUMENT_FOLDERS = ("docs",)
SKIPPED_FOLDERS = ("build", "deps", "node_modules", ".git", ".lake", "third_party", "vendor")

BE = {"am", "is", "are", "was", "were", "be", "been", "being"}
HAVE = {"has", "have", "had"}
# The irregular past participles a passive or a perfect takes; the regular ones end in -ed
IRREGULAR = {"been", "begun", "bitten", "blown", "born", "bought", "brought", "built", "caught", "chosen", "come",
             "cut", "dealt", "done", "drawn", "driven", "eaten", "fallen", "felt", "found", "forgotten", "frozen",
             "given", "gone", "grown", "held", "hidden", "hit", "retained", "known", "positioned", "led", "left", "lent",
             "let", "lost", "made", "meant", "met", "paid", "put", "read", "run", "said", "seen", "sent", "set",
             "displayed", "shut", "sold", "spent", "split", "spoken", "was", "struck", "sung", "taken", "taught",
             "thought", "thrown", "told", "understood", "won", "worn", "written"}
# Words that end in -ing and are no verb form
NOT_VERB_ING = {"anything", "bring", "ceiling", "during", "everything", "king", "nothing", "ring", "sing",
                "something", "spring", "sting", "string", "swing", "thing", "things", "wing", "ping"}
WORD = re.compile(r"[A-Za-z][A-Za-z'-]*")


def documents(paths):
    """Every technical document under the paths, sorted."""
    found = []
    for root in paths:
        if os.path.isfile(root):
            found.append(root)
            continue
        for here, folders, files in os.walk(root):
            # A folder with a .git of its own is another repository, checked against its own ratchet
            folders[:] = [f for f in folders if f not in SKIPPED_FOLDERS and not f.startswith(".")
                          and not os.path.exists(os.path.join(here, f, ".git"))]
            rel = os.path.relpath(here, root).replace("\\", "/")
            inside = rel == "." or rel.split("/")[0] in DOCUMENT_FOLDERS
            for name in files:
                if name.endswith(".md") and (name in DOCUMENT_NAMES or (inside and rel != ".")):
                    found.append(os.path.join(here, name))
    return sorted(set(found))


def prose_lines(text):
    """(line number, prose) for every line of prose: code, tables, headings, comments and license sections out."""
    out = []
    fenced = False
    comment = False
    licence = False
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            fenced = not fenced
            continue
        if fenced:
            continue
        if "<!--" in stripped:
            comment = "-->" not in stripped
            continue
        if comment:
            comment = "-->" not in stripped
            continue
        if stripped.startswith("#"):
            licence = "licen" in stripped.lower()
            out.append((number, ""))
            continue
        if licence or stripped.startswith("|") or stripped.startswith("<") or line.startswith("    "):
            out.append((number, ""))
            continue
        prose = re.sub(r"`[^`]*`", " CODE ", stripped)
        prose = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", prose)
        prose = re.sub(r"https?://\S+", " CODE ", prose)
        prose = re.sub(r"\$[^$]*\$", " CODE ", prose)
        prose = re.sub(r"[*_]{1,3}", "", prose)
        out.append((number, prose))
    return out


def paragraphs(lines):
    """Runs of prose lines, a blank line or a non-prose line closing each; a list item opens its own."""
    block = []
    for number, prose in lines:
        item = re.match(r"^(\d+[.)]|[-*+])\s+", prose)
        if not prose or item:
            if block:
                yield block
            block = []
        if prose:
            block.append((number, prose))
    if block:
        yield block


def sentences(block):
    """(line number, sentence, procedural) for each sentence of a paragraph."""
    text = " ".join(prose for _, prose in block)
    first = block[0][0]
    procedural = re.match(r"^\d+[.)]\s+", block[0][1]) is not None
    text = re.sub(r"^(\d+[.)]|[-*+])\s+", "", text)
    # An abbreviation's stop and a decimal point do not end a sentence
    text = re.sub(r"\b(e\.g|i\.e|etc|vs|cf)\.", r"\1", text)
    for part in re.split(r"(?<=[.!?])\s+(?=[A-Z0-9`(])", text):
        part = part.strip()
        if part:
            yield first, part, procedural


def is_name(word):
    """A technical name: capitalized, carrying a digit or an underscore, or the code stand-in."""
    return word == "CODE" or any(c.isdigit() or c == "_" for c in word) or (word[0].isupper() and word != word.capitalize()) \
        or word.isupper()


def participle(word):
    return word.endswith("ed") or word in IRREGULAR


def findings(path, approved, names):
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    found = []
    for block in paragraphs(prose_lines(text)):
        said = list(sentences(block))
        if len(said) > LIMIT_PARAGRAPH and not said[0][2]:
            found.append((block[0][0], "paragraph", "%d sentences, %d at max" % (len(said), LIMIT_PARAGRAPH)))
        for number, sentence, procedural in said:
            words = WORD.findall(sentence)
            limit = LIMIT_PROCEDURAL if procedural else LIMIT_DESCRIPTIVE
            counted = [w for w in words if w != "CODE"]
            if len(counted) > limit:
                found.append((number, "sentence", "%d words, %d at max: %s" % (len(counted), limit, sentence[:80])))
            lower = [w.lower() for w in words]
            for at, word in enumerate(lower):
                original = words[at]
                if word.endswith("ing") and len(word) > 4 and word not in NOT_VERB_ING and word not in names \
                        and not is_name(original):
                    found.append((number, "ing", word))
                if word in BE and at + 1 < len(lower):
                    after = lower[at + 2] if lower[at + 1] in ("not", "also", "then", "now") and at + 2 < len(lower) \
                        else lower[at + 1]
                    if participle(after) and after not in names:
                        found.append((number, "passive", "%s %s" % (word, after)))
                    elif after.endswith("ing") and after not in NOT_VERB_ING and after not in names:
                        found.append((number, "tense", "%s %s" % (word, after)))
                if word in HAVE and at + 1 < len(lower) and participle(lower[at + 1]) and lower[at + 1] not in names:
                    found.append((number, "tense", "%s %s" % (word, lower[at + 1])))
                if approved and not is_name(original) and word not in approved and word not in names and len(word) > 2:
                    found.append((number, "word", word))
    return found


def word_list(path):
    words = set()
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            first = line.split("\t")[0].strip().lower()
            if first and not first.startswith("#"):
                words.add(first)
    return words


def ratchet_read(path):
    allowed = {}
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                part = line.rstrip("\n").split("\t")
                if len(part) == 2 and part[1].isdigit():
                    allowed[part[0]] = int(part[1])
    return allowed


def main(argv):
    # A finding quotes the document, and a console's code page may hold none of its characters
    sys.stdout.reconfigure(errors="replace")
    args = list(argv[1:])
    ratchet = None
    write = None
    approved = set()
    if "--ratchet" in args:
        at = args.index("--ratchet")
        ratchet = args[at + 1]
        del args[at:at + 2]
    if "--ratchet-write" in args:
        at = args.index("--ratchet-write")
        write = args[at + 1]
        del args[at:at + 2]
    while "--words" in args:
        at = args.index("--words")
        approved |= word_list(args[at + 1])
        del args[at:at + 2]
    paths = args or ["."]
    # The names file sits beside the ratchet, and the ratchet sits beside this file. A run with no ratchet reads
    # the same names: a listed finding is a finding the ratchet counts.
    base = os.path.dirname(os.path.abspath(ratchet or write or __file__))
    names_file = os.path.join(base, "ste_names.txt")
    names = word_list(names_file) if os.path.isfile(names_file) else set()
    counts = {}
    displayed = 0
    for path in documents(paths):
        key = os.path.relpath(path).replace("\\", "/")
        rows = findings(path, approved, names)
        counts[key] = len(rows)
        if ratchet is None and write is None:
            for number, rule, what in rows:
                print("%s:%d: %s: %s" % (key, number, rule, what))
                displayed += 1
    if write is not None:
        allowed = ratchet_read(write)
        with open(write, "w", encoding="utf-8", newline="\n") as handle:
            for key in sorted(counts):
                handle.write("%s\t%d\n" % (key, min(counts[key], allowed.get(key, counts[key]))))
        print("ste: %d documents written to %s" % (len(counts), write))
        return 0
    if ratchet is not None:
        allowed = ratchet_read(ratchet)
        over = [(key, n, allowed.get(key, 0)) for key, n in sorted(counts.items()) if n > allowed.get(key, 0)]
        for key, n, max in over:
            print("ste: %s carries %d findings, %d allowed: run ste_check.py %s to see them" % (key, n, max, key))
        return 1 if over else 0
    total = sum(counts.values())
    print("ste: %d findings in %d documents" % (total, len(counts)))
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
