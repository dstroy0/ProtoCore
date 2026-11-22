#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
"""The ASD-STE100 repairs that need no new word, made in the comments of the tree and in its technical documents.

    python utils/maint/prose/ste_fix.py [paths]            report each change and the findings it removes

    python utils/maint/prose/ste_fix.py --write [paths]    write the changes

With no path, the tool reads each file of the prose ratchet and each technical document of the ste ratchet.

The tool makes four changes, and each change only moves, removes or inflects a word that the sentence holds:

  tense      "has", "have" or "had" before a past participle goes out, and the simple past stays. The participle
             is a regular -ed form or an irregular form that is the same as its past. A modal or "to" before
             the auxiliary stops the change, because the sense of the sentence then changes.

  tense      "is" or "are" before an -ing form with an object after it goes out. The simple present of the same
             verb takes the place of the -ing form. The present form must be a word of the voice table.

  sentence   a semicolon between two clauses of a long sentence becomes a full stop, and the next word gets a capital
             letter. The next word must be an English word of the voice table. A semicolon in code, in a quotation
             or in brackets stays.

  sentence   ", and", ", so", ", but" or a colon before a clause with a pronoun subject becomes a full stop. The tool
             removes "and". It keeps "so" or "but" with a capital letter, and the next word gets a capital letter. A
             comma or a colon before them on the same line can mark a list, and the line then stays.

The checker counts the findings of the file before and after each change. A change stays only where the count of
the file falls and no rule of the file gets more findings. The docs check then holds each file to its ratchet at
the next commit.
"""

import os
import re
import sys

PROSE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, PROSE)

from docs_check.prose import prose_only  # noqa: E402
from docs_check.ratchet import ratchet_read  # noqa: E402
from docs_check.repository import REPOSITORY  # noqa: E402
from docs_check.ste import checker, ste_findings  # noqa: E402

LETTERS = r"[A-Za-z]+"

PROSE_RATCHET = os.path.join(PROSE, "prose_ratchet.tsv")
STE_RATCHET = os.path.join(PROSE, "ste_ratchet.tsv")
VOICE = os.path.join(PROSE, "voice.tsv")
NAMES = os.path.join(PROSE, "ste_names.txt")
DOCUMENT = os.path.join(REPOSITORY, "build", "gate", "ste_fix_document.md")

HAVE = re.compile(r"(?<![\w'-])(has|have|had) (?=([a-z]+)\b)")
MODAL = {"to", "can", "could", "may", "might", "must", "shall", "should", "will", "would", "not", "never"}
# The irregular participles that are the same as their past, from the participles that ste_check.py knows
SAME_AS_PAST = {"brought", "built", "bought", "caught", "cut", "dealt", "displayed", "felt", "found", "held", "hit",
                "led", "left", "lent", "let", "lost", "made", "meant", "met", "paid", "positioned", "put", "read",
                "retained", "said", "sent", "set", "shut", "sold", "spent", "split", "struck", "taught", "thought",
                "told", "understood", "won"}
# The words that end in -ed and are no participle
NOT_PARTICIPLE = {"bed", "bleed", "breed", "embed", "exceed", "feed", "heed", "need", "proceed", "red", "seed", "shed",
                  "speed", "succeed", "shred", "weed"}
# The words that can follow a participle that is a verb. A noun after the participle makes it an adjective, and the
# auxiliary then stays.
AFTER_VERB = {"a", "an", "the", "this", "that", "these", "those", "it", "its", "them", "their", "his", "her", "him",
              "us", "you", "me", "every", "each", "all", "no", "one", "to", "in", "on", "at", "by", "from", "into",
              "out", "up", "down", "off", "over", "back", "before", "after", "since", "once", "and", "or", "but",
              "as", "with", "for", "of", "here", "there", "so", "than", "when", "where", "while", "if"}
SEMICOLON = re.compile(r"(?<=[A-Za-z0-9)\]\"']); +([a-z][a-z]+)\b(?![\w(.\[:/-])")
PROGRESSIVE = re.compile(r"(?<![\w'-])(is|are) ([a-z]+ing)\b")
# The words that open a direct object. A progressive verb with an object after it is a verb, and the simple present
# takes its place. Without an object the -ing word can be an adjective, and it stays.
OBJECT_OPENERS = {"a", "an", "the", "this", "that", "these", "those", "it", "its", "them", "their", "each", "every",
                  "all", "any", "no", "one", "his", "her"}
JOINED = re.compile(r"(?<=[A-Za-z0-9)\]\"'])(?:, (and|so|but)|(:)) ([a-z][a-z]+)\b")
# The pronouns that open a clause with its own subject. A conjunction before one of them joins two clauses, and a full
# stop can take the place of the comma and the conjunction. A determiner such as "the" can open the next item of a
# list as well as a clause, and it is not one of them.
SUBJECT_OPENERS = {"it", "they", "we", "there"}
# A comment region that holds one of these is code or a formula, and the tool does not split it
CODE_SIGNS = ("{", "}", " = ", "==", "->", "::", "for (", "while (", "&&", "||", "+=", "++")
# The two docstring marks, made from one quote each, so that the docs check does not read a mark in this code
DOUBLE_MARK = '"' * 3
SINGLE_MARK = "'" * 3
OPENERS = re.compile(r"^\s*(?:#+|/(?:/+|\*+)|\*+(?!/)|\"{3}|'{3})\s?")


def word_set(path, column=0):
    """The words of the first column of a table or a list, in lower case."""
    found = set()
    if not os.path.isfile(path):
        return found
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            first = line.split("\t")[column].strip().lower()
            if first and not first.startswith("#"):
                found.add(first)
    return found


ENGLISH = word_set(VOICE)
TECHNICAL = word_set(NAMES)


def regions(path, lines):
    """(line index, start column, end column) of the comment prose on each line that the docs check reads."""
    if path.endswith(".md"):
        out = []
        fenced = False
        for index, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith(("```", "~~~")):
                fenced = not fenced
                continue
            if fenced or not stripped or stripped.startswith(("#", "|", "<")) or line.startswith("    "):
                continue
            out.append((index, 0, len(line)))
        return out
    said = prose_only(path, lines)
    out = []
    for index, view in enumerate(said):
        raw = lines[index]
        if not view.strip() or not raw.endswith(view):
            continue
        start = len(raw) - len(view)
        end = len(raw)
        part = raw[start:end]
        # A line that holds code before its comment marker is not prose before the marker
        opener = OPENERS.match(part)
        if opener:
            start += opener.end()
        elif path.endswith(".py") and (DOUBLE_MARK in part or SINGLE_MARK in part):
            continue
        part = raw[start:end]
        for closer in ("*/", DOUBLE_MARK, SINGLE_MARK):
            if closer in part:
                end = start + part.index(closer)
                part = raw[start:end]
        if "/*" in part:
            start += part.index("/*") + 2
        out.append((index, start, end))
    return out


def count(path, lines):
    """The ASD-STE100 findings of the file, by rule."""
    if path.endswith(".md"):
        os.makedirs(os.path.dirname(DOCUMENT), exist_ok=True)
        with open(DOCUMENT, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines) + "\n")
        rows = [rule for _, rule, _ in checker().findings(DOCUMENT, set(), TECHNICAL)]
    else:
        rows = [what.split(":")[0] for _, what in ste_findings(prose_only(path, lines))]
    tally = {}
    for rule in rows:
        tally[rule] = tally.get(rule, 0) + 1
    return tally


def better(before, after):
    """Whether the change removes one of the findings and gives no rule more findings."""
    return sum(after.values()) < sum(before.values()) and all(after.get(rule, 0) <= before.get(rule, 0)
                                                               for rule in after)


def outside_code(part, at):
    """Whether the column is out of quotation and out of brackets."""
    before = part[:at]
    return (before.count("`") % 2 == 0) and (before.count("(") <= before.count(")")) \
        and (before.count("[") <= before.count("]"))


def third_person(base):
    """The form of a verb for one subject in the simple present: the base with -s, -es or -ies."""
    if base.endswith(("s", "sh", "ch", "x", "z", "o")):
        return base + "es"
    if base.endswith("y") and len(base) > 1 and base[-2] not in "aeiou":
        return base[:-1] + "ies"
    return base + "s"


def present_form(ing, plural):
    """The simple present of the -ing form, for many subjects where `plural`, else for one. The form must be a word of
    the voice table, else the answer is None."""
    stem = ing[:-3]
    bases = [stem, stem + "e"]
    if len(stem) > 2 and stem[-1] == stem[-2]:
        bases.append(stem[:-1])
    for base in bases:
        if base in ENGLISH:
            form = base if plural else third_person(base)
            if form in ENGLISH:
                return form
    return None


def candidates(part):
    """(start, end, replacement) of each change in the prose of one line, from its end to its start."""
    found = []
    for hit in PROGRESSIVE.finditer(part):
        following = re.match(r"\s*([A-Za-z]+)", part[hit.end():])
        if not following or following.group(1).lower() not in OBJECT_OPENERS:
            continue
        if hit.group(2) in TECHNICAL or not outside_code(part, hit.start()):
            continue
        form = present_form(hit.group(2), hit.group(1) == "are")
        if form is not None:
            found.append((hit.start(), hit.end(), form))
    # a comma before the joined clause on the same line can mark a list, and the tool does not split the line there
    for hit in JOINED.finditer(part):
        joint = hit.group(1) or hit.group(2)
        word = hit.group(3)
        before = part[:hit.start()]
        if (word not in SUBJECT_OPENERS) or ("," in before) or (":" in before) or not outside_code(part, hit.start()):
            continue
        if len(re.findall(LETTERS, before)) < 4 or len(re.findall(LETTERS, part[hit.end():])) < 3:
            continue
        if joint in ("and", ":"):
            found.append((hit.start(), hit.start(3) + 1, ". " + word[0].upper()))
        else:
            # "so" and "but" stay, each with a capital letter
            found.append((hit.start(), hit.start(1) + 1, ". " + joint[0].upper()))
    for hit in HAVE.finditer(part):
        participle = hit.group(2)
        regular = participle.endswith("ed") and len(participle) > 4 and participle not in NOT_PARTICIPLE
        if not (regular or participle in SAME_AS_PAST) or participle in TECHNICAL:
            continue
        previous = re.findall(r"[A-Za-z']+", part[:hit.start()])
        if previous and previous[-1].lower() in MODAL:
            continue
        if not outside_code(part, hit.start()):
            continue
        rest = part[hit.end() + len(participle):]
        following = re.match(r"\s*([A-Za-z]+)", rest)
        if following and following.group(1).lower() not in AFTER_VERB:
            continue
        found.append((hit.start(), hit.end(), ""))
    if not any(sign in part for sign in CODE_SIGNS):
        for hit in SEMICOLON.finditer(part):
            word = hit.group(1)
            if word not in ENGLISH or word in TECHNICAL or not outside_code(part, hit.start()):
                continue
            words_before = len(re.findall(LETTERS, part[:hit.start()]))
            if words_before < 3:
                continue
            found.append((hit.start(), hit.start(1) + 1, ". " + word[0].upper()))
    return sorted(found, reverse=True)


def repaired(path, lines):
    """The lines with each change that the checker accepts, and the count of the changes."""
    lines = list(lines)
    tally = count(path, lines)
    made = 0
    for index, start, end in regions(path, lines):
        for at, stop, replacement in candidates(lines[index][start:end]):
            line = lines[index]
            changed = line[:start + at] + replacement + line[start + stop:]
            trial = lines[:index] + [changed] + lines[index + 1:]
            after = count(path, trial)
            if better(tally, after):
                lines = trial
                tally = after
                made += 1
    return lines, made


def targets(paths):
    """The files to repair: each named path, or each file of the two ratchets."""
    if paths:
        return [os.path.abspath(one) for one in paths]
    # A page and a .tex of the prose ratchet carry no ASD-STE100 findings there. A page of the ste ratchet does.
    comments = [name for name in (ratchet_read(PROSE_RATCHET) or {}) if not name.endswith((".md", ".tex"))]
    names = set(comments) | set(checker().ratchet_read(STE_RATCHET))
    return sorted(os.path.join(REPOSITORY, name) for name in names if os.path.isfile(os.path.join(REPOSITORY, name)))


def main(argv):
    write = "--write" in argv
    paths = [one for one in argv[1:] if not one.startswith("--")]
    total = 0
    for path in targets(paths):
        try:
            with open(path, encoding="utf-8", errors="strict", newline="") as handle:
                text = handle.read()
        except UnicodeDecodeError:
            print("    -  %s is not UTF-8, and the tool does not change it" % os.path.relpath(path, REPOSITORY))
            continue
        ending = "\r\n" if "\r\n" in text else "\n"
        lines = text.split(ending)
        fixed, made = repaired(path, lines)
        if not made:
            continue
        total += made
        print("%5d  %s" % (made, os.path.relpath(path, REPOSITORY).replace("\\", "/")))
        if write:
            with open(path, "w", encoding="utf-8", newline="") as handle:
                handle.write(ending.join(fixed))
    print("%d changes%s" % (total, "" if write else ", nothing written: give --write to write them"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
