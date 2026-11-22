#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# Which files are read, which are walked past, and how a tree is walked.
#

import os

from .verbatim import verbatim_root


# Prose lives in pages, in comments, in the research papers and in the build, and the same voice writes all
# four.
#
# .tex belongs here because the research papers are the longest continuous prose in the tree and
# the only part written to be read straight through. Leaving the extension out leaves them unread.
#
# BUILD FILES BELONG HERE FOR THE SAME REASON. A comment in a CMakeLists.txt makes the same claim a
# comment in a header makes, in the same voice, to the same reader. An extension this tuple cannot
# open reports "0 file(s) checked" and exits 2, which reads like a pass.
#
# AN EXTENSION AND A PATTERN HIDE A SITE TWICE, and that is the trap to watch for. A form the
# locale stage already names is hidden once, by the extension list. A form no pattern reaches is
# hidden twice, by the extension list AND by the pattern being too narrow. Adding the extension
# alone makes the first type fire, which reads as coverage, and leaves the second type invisible
# further down the same file. Both halves are needed to see either, which is why they are one pass.
BUILD_SUFFIXES = (".sh", ".ps1", ".cmake", ".yml", ".yaml")

# Named, not suffixed. A build file is as likely to be named as it is to be extended, and an
# extension list cannot express `CMakeLists.txt`.
BUILD_NAMES = ("CMakeLists.txt", "Makefile", "GNUmakefile", "Dockerfile")

# A git hook has no extension at all, and no extension list can ever select it. A sweep by
# extension drops a hook and takes the gate with it. orior retains its own hooks in .githooks/ and
# every one of them is a shell script full of comments.
HOOK_NAMES = (
    "pre-commit",
    "commit-msg",
    "prepare-commit-msg",
    "pre-push",
    "pre-rebase",
    "post-merge",
    "post-checkout",
)

# The C family as this tree actually writes it. Naming .c and .h alone names the two extensions
# the tree barely uses and omits the two it is written in: against 3 .c and 9 .h there are 205 .cu
# and 3 .cpp, and every one of them carries argued note blocks in the same voice as a header.
# prose_only needs nothing added for them, since it falls through to the C comment branch for any
# extension it does not name and that branch is already right for CUDA.
#
# .html is here for the same reason and reads the same way: the view templates carry their argument
# in the `//` and `/* */` comments of their script blocks. A `<!-- -->` comment is NOT read, and
# this tree writes none. Neither is the visible text of a page, which wants a tag stripper.
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".cu", ".html")

CHECKED = (".md", ".py", ".tex") + SOURCE_SUFFIXES + BUILD_SUFFIXES


def build_file(path):
    """Whether this path is a build file, by extension or by the name it was given.

    Read as one question and not two. A caller cannot answer half of it. submission_check.py
    imports CHECKED and would otherwise select a .cmake and hand it to the C extractor.
    """
    name = os.path.basename(path)
    return name in BUILD_NAMES or name in HOOK_NAMES or path.endswith(BUILD_SUFFIXES)


def checked_file(path):
    """Whether this tool reads this path at all."""
    return path.endswith(CHECKED) or build_file(path)

# Fetched or generated, and none of it authored here. target is what cargo writes for ui.
# fixtures holds the positive control for machine_distance.py, written deliberately in the register
# being detected. Repairing it deletes the only sample of the thing.
#
# A directory holding its own `.git` is the root of another checkout: a linked worktree, which is a
# full copy of this tree, or a repository nested in it. Neither is read. A repository with N linked
# worktrees inside it would report every finding N+1 times, at a ratio set by how many worktrees
# exist, and the copies would bury the real site of each finding among them.

SKIP_DIRS = (
    ".git",
    "build",
    "site",
    "deps",
    "__pycache__",
    ".vscode",
    "fixtures",
    "target",
)


def retained_dirs(here, dirs):
    """The directories under `here` a walk goes into: none in SKIP_DIRS and none that is another checkout."""
    return [
        one for one in dirs if one not in SKIP_DIRS and not os.path.exists(os.path.join(here, one, ".git"))
    ]


# The gate checks the markdown and skips the chapters built from it. The exemptions a verbatim text
# is held under (a quoted span, a quiet block, a .verbatim marker) are read in the .md and are lost
# in the conversion, so the same quote passes in the .md and fails in its chapter. The cost of the
# rule is that markdown the converter leaves in a chapter is not caught here.
#
# The chapters carry no comment line saying they are generated; the theory research papers hold no TeX
# comments. theory_tex.py manages every research paper under workbooks/ or thought_experiments/ that holds a
# README.md, and every file in such a research paper's chapters/ is its output.
def generated_chapter(path):
    """Whether a .tex was written by theory_tex.py from a markdown source."""
    if not path.endswith(".tex"):
        return False
    chapters = os.path.dirname(os.path.abspath(path))
    research_paper = os.path.dirname(chapters)
    return (
        os.path.basename(chapters) == "chapters"
        and os.path.basename(os.path.dirname(research_paper)) in ("workbooks", "thought_experiments")
        and os.path.isfile(os.path.join(research_paper, "README.md"))
    )


# What a built page carries where its template carries a placeholder. A builder replaces the marker
# with the data, so a page holding the assignment and not the marker was written by a tool.
BUILT_PAGE = "var DATA = {"
PAGE_MARKER = "_DATA*/null"

# How far into a page to look. The assignment sits near the top of the script and a built page runs
# to hundreds of kilobytes, almost all of it one line of data.
PAGE_HEAD = 400000


def generated_page(path):
    """Whether an .html file was written by a builder instead of by a person.

    A built page's prose is its template's prose. Reporting both counts one defect twice and points
    the writer at the copy, which the next build overwrites. The template is the file to fix.
    """
    if not path.endswith(".html"):
        return False
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            head = handle.read(PAGE_HEAD)
    except OSError:
        return False
    return BUILT_PAGE in head and PAGE_MARKER not in head


def walk_markdown(roots, ledger=None):
    """Every prose file under the given roots, taking a file argument as itself.

    Source files are included because a comment makes the same claims a page does, in the same
    voice, to the same reader. Checking only the pages leaves the register unchecked everywhere it
    is actually written.

    Directories that hold fetched or generated material are skipped. A published page under `site`
    is a copy of one already checked here, and reporting it twice trains a reader to skip the output.

    Selection goes through checked_file, which answers for a named file as well as an extension. A
    git hook has no extension and a CMakeLists.txt is a name, and an endswith test on extensions
    alone sees neither.

    A file holding text reproduced from a third party is declined here and named in the ledger. It
    is the only exclusion in this file that stops the read instead of shaping it, because a finding
    inside somebody else's document is a finding against its author and there is nothing for a
    reader of this report to decide about one.
    """
    found = []
    for root in roots:
        if os.path.isfile(root):
            if checked_file(root):
                found.append(root)
            continue
        for here, dirs, names in os.walk(root):
            dirs[:] = retained_dirs(here, dirs)
            found.extend(
                os.path.join(here, name)
                for name in names
                if checked_file(os.path.join(here, name))
            )
    # This package writes down every phrase it bans and matches itself on nearly all of them.
    # Quieting them one pair at a time with the markers below buries the table under its own
    # pragmas, and a reader scrolling past a hundred of them stops reading them. The whole
    # directory is skipped instead.
    mine = os.path.dirname(os.path.abspath(__file__))
    my_dir = os.path.dirname(mine)
    retained = []
    for one in found:
        if os.path.dirname(os.path.abspath(one)) == mine:
            continue
        # This tool's own test files carry banned prose on purpose, to prove the gate flags it.
        # Repairing them would break the tests. They sit in the directory holding this package and
        # are excluded here, the exclusion recorded like every other. A fixtures/ directory is
        # already skipped by SKIP_DIRS; these are the tests that live next to the gate.
        one_name = os.path.basename(one)
        if (os.path.dirname(os.path.abspath(one)) == my_dir) and one_name.startswith(
            "test_docs_check"
        ):
            if ledger is not None:
                ledger.note(
                    "gate self-test",
                    "carries banned prose to prove the gate flags it",
                    one.replace("\\", "/"),
                )
            continue
        held = verbatim_root(one)
        if held:
            if ledger is not None:
                ledger.note("verbatim third-party", held[1], one.replace("\\", "/"))
            continue
        if generated_chapter(one):
            if ledger is not None:
                ledger.note(
                    "generated chapter",
                    "written by theory_tex.py from a markdown file, and the markdown is checked",
                    one.replace("\\", "/"),
                )
            continue
        if generated_page(one):
            if ledger is not None:
                ledger.note(
                    "generated page",
                    "written by a builder from a template, and the template is checked",
                    one.replace("\\", "/"),
                )
            continue
        retained.append(one)
    return retained
