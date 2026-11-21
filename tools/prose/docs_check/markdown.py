#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The defects that reach a reader as a broken page: empty tables, dead links, leftovers.
#

import os
import re


# A markdown table separator: | --- | --- |
SEPARATOR = re.compile(r"^\s*\|[\s:|-]+\|\s*$")
ROW = re.compile(r"^\s*\|")

# A relative markdown link, skipping anything with a scheme and anything anchored to a heading.
LINK = re.compile(r"\[[^\]]*\]\(([^)#][^)]*)\)")
# Markdown renders nothing inside an inline code span as a link. A formula set as code, such as
# `C[a, b](v)`, is blanked out before LINK reads the line.
CODE_SPAN = re.compile(r"`[^`\n]*`")

# A Doxygen cross-reference written in markdown link syntax: [`HTTP_10`](@ref HTTP_10). Doxygen
# resolves the target against the symbol table it builds from the source. The word after the command
# is an identifier. The filesystem has no answer to give about it, and producing one means reading
# Doxygen's tag file, which this tool does not do.
#
# Both definitions of every command are accepted, since Doxygen takes @ref and \ref alike.
DOXYGEN_TARGET = re.compile(r"^[@\\](ref|subpage|page|link|anchor|cite|see|copydoc)\b")

# C declarator syntax that LINK matches by accident. A lambda in a fenced example writes its capture
# list in square brackets and its parameter list in parentheses. `[](uint8_t slot, HttpReq *req)`
# is character for character the shape a markdown link has.
#
# Three signals, each one sufficient, and each one chosen because a relative path cannot carry it:
# a pointer star anywhere in the target, a C type qualifier or specifier opening it, or a
# comma-separated run where every item is two words. A path with a comma in it is legal and rare,
# and `docs/a.md, docs/b.md` fails the last test because neither item has an interior space.
DECLARATOR_HEAD = re.compile(
    r"^(const|volatile|unsigned|signed|struct|enum|union|static)\s"
)


def empty_tables(lines):
    """A separator row with no data row under it renders as a table with a head and no body."""
    found = []
    for at, line in enumerate(lines):
        if not SEPARATOR.match(line):
            continue
        following = lines[at + 1] if (at + 1) < len(lines) else ""
        if not ROW.match(following):
            found.append((at + 1, "table header with no rows under it"))
    return found


# Markdown that survived the conversion into .tex. Every one of these is valid LaTeX. The research paper
# compiles with no error, no warning and no dropped glyph, and carries the artifact to the archive.
#
# The em dash rule above could not see any of it. A --- is an em dash after typesetting and the
# check was looking for the character. The one form a converter actually produces was the one
# form it missed.
#
# All three were found by reading rendered pages, and this exists to stop that. In delta_null a
# --- set as a stray dash above the attribution on printed page 53, two claims wrapped in asterisks
# set as literal asterisks, and seventeen titles wrapped in escaped underscores set as literal
# underscores around Don Quixote, Faust and the Kalevala.
#
# Read against the stripped prose, and that retains a filename out of the count: tex_prose
# removes \texttt{} with its braces. The escaped underscores inside a path are gone before this sees
# the line.
MARKDOWN_RULE = re.compile(r"^\s*-{3,}\s*$")
MARKDOWN_BOLD = re.compile(r"\*\*(?=\S)[^*]*\S\*\*")
MARKDOWN_ITALIC = re.compile(r"(?<![A-Za-z0-9])\\_(?=[A-Za-z])[^\\]*\\_(?![A-Za-z0-9])")

# A drawing, not emphasis. The SHA-256 shadow chapters plot one row per bit and the asterisks in
# those rows are ink. Three or more of the characters a plot is ruled with says so.
ASCII_ART = re.compile(r"[#=|+~^]{3,}")


def markdown_leftovers(lines):
    """Markdown left in a .tex source, which typesets as punctuation a reader sees on the page."""
    found = []
    for at, line in enumerate(lines):
        if ASCII_ART.search(line):
            continue
        if MARKDOWN_RULE.match(line):
            found.append(
                (at + 1, "markdown rule left in .tex, which typesets as an em dash")
            )
        if MARKDOWN_BOLD.search(line):
            found.append(
                (
                    at + 1,
                    "markdown bold left in .tex, which typesets as literal asterisks",
                )
            )
        if MARKDOWN_ITALIC.search(line):
            found.append(
                (
                    at + 1,
                    "markdown italics left in .tex, which typesets as literal underscores",
                )
            )
    return found


def path_candidate(target):
    """Whether a matched link target is a path at all, before asking whether the path is there.

    dead_links is a structural check and a structural finding fails a commit. A target this
    returns True about has to be something the filesystem can actually answer for. Two shapes wear
    markdown link syntax without being paths. This test sits in front of os.path.exists instead of
    in an exemption list somewhere.

    Doxygen references. [`HTTP_10`](@ref HTTP_10) resolves against documented symbols, and a finding
    on one reports a working cross-reference as a broken link.

    C declarators. A lambda in a fenced example writes `[](const char *user, const char *pass)`, and
    a parenthesized group following a bracketed one is the shape LINK looks for.

    No target skipped here names a path that is on disk. The type-keyword head and the
    comma-separated list are retained for the parameter list that has neither star nor keyword, as in `(uint8_t slot, size_t len)`. The comma rule is the
    loosest of the three and is bounded to items of two words each. `docs/a.md, docs/b.md` stays
    a pair of paths.
    """
    if (not target) or ("://" in target) or target.startswith("/"):
        return False
    if DOXYGEN_TARGET.match(target):
        return False
    if "*" in target:
        return False
    if DECLARATOR_HEAD.match(target):
        return False
    parts = [one.strip() for one in target.split(",")]
    if (len(parts) > 1) and all(" " in one for one in parts):
        return False
    return True


def dead_links(path, lines):
    """A relative link to a file that is not there. Absolute and external links are left alone.

    A target that is not a path is skipped by path_candidate before anything is read from disk.
    """
    here = os.path.dirname(path)
    found = []
    fenced = False
    for at, line in enumerate(lines):
        # a fenced code block is code, not prose: a bracket pair in it is no link
        if line.lstrip().startswith(("```", "~~~")):
            fenced = not fenced
            continue
        if fenced:
            continue
        line = CODE_SPAN.sub(lambda span: " " * len(span.group(0)), line)
        for hit in LINK.finditer(line):
            target = hit.group(1).split("#")[0].strip()
            if not path_candidate(target):
                continue
            if not os.path.exists(os.path.join(here, target)):
                found.append((at + 1, "link to a file that is not there: %s" % target))
    return found
