#!/usr/bin/env python3
# orior - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later OR LicenseRef-Commercial OR LicenseRef-Educational
#
# The prose view of a file: comments, tex bodies, and the lines that are neither.
#

import re

from .files import build_file
from .legal import legal_blank
from .quoting import QUIET_CLOSE, quieted



# The audit on the mechanism above, because the mechanism is easy to misuse. A marker pair protects
# a quotation, and a quotation has edges: it opens and closes where a sentence does. A pair reached
# for to silence a finding lands wherever the token sits, in the middle of a sentence. A correct
# pair closes on a finished sentence; a misused one closes on a fragment.
#
# Only the closing marker is tested. An opening marker sits above the quoted material in both shapes
# and tells them apart from nothing.
#
# Both halves of the tell have to agree before anything is reported: no sentence-ending punctuation
# before the marker, and a lowercase word after it. Either half alone fires on a table that ends in
# a bracket, or on a paragraph that happens to open lowercase.
#
# Reported and never errored. The evidence is six pairs, four correct against two misused, and the
# failure mode is a legitimate quotation of a fragment, which is a real thing to want to write. The
# one correct pair quoting two words closes cleanly because the sentence around it was written to
# close cleanly, and that will not hold for every future one. Raising this to breaking wants more
# pairs to have been right about, and not more confidence about six.
SENTENCE_END = (".", "?", "!", ":", ";", '"', "'", ")", "`")
CONTINUATION = re.compile(r"^[a-z]")


def near_marker(lines, at, step):
    """The nearest line carrying text on one side of a marker, without its comment marker.

    Blank lines and bare comment markers are stepped over. A pair set off by an empty comment line
    above and below is the shape that reads best, and stopping on one would report every block that
    was arranged with any care.
    """
    walk = at + step
    while 0 <= walk < len(lines):
        body = lines[walk].strip().lstrip("#/*%").strip()
        if body:
            return body
        walk += step
    return None


def marker_edges(lines):
    """Findings for a quiet block whose closing marker cuts a sentence in half."""
    found = []
    for at, line in enumerate(lines):
        if QUIET_CLOSE not in line:
            continue
        before = near_marker(lines, at, -1)
        after = near_marker(lines, at, 1)
        if (before is None) or (after is None):
            continue
        if before.endswith(SENTENCE_END) or not CONTINUATION.match(after):
            continue
        found.append(
            (
                at + 1,
                "quiet block closes in the middle of a sentence. A marker pair "
                "protects a quotation, and this pair is hiding a finding",
            )
        )
    return found


def tex_prose(lines):
    """A LaTeX source with its markup blanked and its sentences left, line numbers preserved.

    A .tex file is prose all the way down, unlike a source file where prose sits in the comments.
    What has to come out is the markup, and only the markup that is not language: a \\textbf or an
    \\emph wraps a sentence somebody wrote and it stays, while a \\texttt wraps a path and a \\label
    wraps an identifier, and reading either as prose reports findings against a filename.

    Math is dropped whole. A displayed equation is symbols, and an inline $x$ carries no sentence.

    Nothing here parses TeX. It removes the constructs that produce false findings and leaves the
    rest, the same trade prose_only already makes about string literals in source.
    """
    retained = []
    for line in lines:
        held = line

        # Comment to end of line, on an unescaped percent. A note to a co-author is prose and would
        # be worth checking, but it is also where a stray brace or a half sentence lives. It goes
        # with the markup and is not reported against.
        held = re.sub(r"(?<!\\)%.*$", "", held)

        # Math, inline and displayed. Done before commands, since a command inside math goes with
        # it.
        held = re.sub(r"\$\$.*?\$\$", " ", held)
        held = re.sub(r"(?<!\\)\$.*?(?<!\\)\$", " ", held)
        held = re.sub(r"\\\[.*?\\\]", " ", held)
        held = re.sub(r"\\\(.*?\\\)", " ", held)

        # A quotation in TeX's own markup, ``like this'', is a sentence somebody else wrote, and it
        # goes the way a string literal goes in source. TeX marks the quotation itself, and no
        # comment in the file has to.
        held = re.sub(r"``.*?''", " ", held)

        # Commands whose braces hold an identifier and never a sentence. The argument goes with the
        # command. \allowbreak{} appears mid-path in this tree's citations and would otherwise leave
        # its fragments behind as words.
        held = re.sub(
            r"\\(texttt|verb|url|href|path|label|ref|eqref|cite\w*|input|include|"
            r"includegraphics|usepackage|documentclass|bibliography\w*|hypersetup|"
            r"newcommand|renewcommand|def|allowbreak|textbackslash)\s*(\[[^\]]*\])?"
            r"(\{[^{}]*\})*",
            " ",
            held,
        )

        # Environment openers and closers, which name the environment and carry no sentence.
        held = re.sub(
            r"\\(begin|end)\s*\{[^{}]*\}(\[[^\]]*\])?(\{[^{}]*\})*", " ", held
        )

        # Every remaining command retains its braces, since \textbf{a sentence} is a sentence. The
        # command name itself goes, and so do the braces around it.
        held = re.sub(r"\\[A-Za-z@]+\s*(\[[^\]]*\])?", " ", held)
        held = held.replace("{", " ").replace("}", " ")

        # Alignment and cell separators in a table, which glue unrelated words into a phrase.
        held = held.replace("&", " ").replace("\\\\", " ")

        retained.append(held)
    return quieted(retained)


# A quoted or apostrophized span, blanked before a `#` is looked for. A hash inside a string is
# not read as a comment marker. Carried from ai_words.py, which this pass supersedes.
STRING_SPAN = re.compile(r"\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'")


def hash_tail(line):
    """The `#` comment on one line, or empty where there is none.

    The hash has to open a word: at the start of the line, or after whitespace. Without that rule
    `$#` in a shell script and `${#name}` in both shell and CMake read as comment markers, and a run
    of argument arithmetic gets scanned as prose.

    Strings are masked first. `message("count: #1")` is not read as a comment either.
    """
    masked = STRING_SPAN.sub(lambda hit: " " * len(hit.group(0)), line)
    for at, character in enumerate(masked):
        if character != "#":
            continue
        if at == 0 or masked[at - 1].isspace():
            return line[at:]
    return ""


def comment_prose(lines):
    """The comment text of a build file, with the rest blanked and line numbers preserved.

    Blanked. A finding still names the line a reader has to open. Handles the
    `#` form shell, CMake, YAML and make all share, and PowerShell's `<# ... #>` block.

    A shebang is dropped. It is the only line of a shell script that is an instruction to the kernel
    and not a sentence, and `#!/usr/bin/env python3` reported nothing but was read every time.

    THE CMake STRING IS DELIBERATELY NOT READ. A CMakeLists.txt can put a banned phrase inside a
    `set(... CACHE BOOL "...")` description, which is a string and reaches a person through
    `ccmake`. Reading it wants a CMake parser, and guessing at one
    would report every quoted path in every add_custom_command. Named here because it is a known
    gap and not an oversight.
    """
    retained = []
    in_block = False
    for at, line in enumerate(lines):
        if at == 0 and line.startswith("#!"):
            retained.append("")
            continue
        if in_block:
            retained.append(line)
            if "#>" in line:
                in_block = False
            continue
        if "<#" in line:
            in_block = "#>" not in line
            retained.append(line)
            continue
        retained.append(hash_tail(line))
    return retained


def prose_only(path, lines, ledger=None):
    """The comment and docstring lines of a source file, with the code blanked out.

    Line numbers are preserved by replacing code with an empty string instead of dropping it, and a
    finding still points at the line a reader has to open. Deliberately crude about string literals:
    a banned word inside one is worth looking at anyway, since it is usually output text.

    THE LEGAL BLANKING HAPPENS HERE AND NOWHERE ELSE, on every branch, and that retains the four
    stages from each carrying a skip of their own. em_dashes, markdown_leftovers and banned_tokens
    all read what this returns. An em dash inside a copyright grant and a British form inside
    one go quiet together and by one rule. empty_tables and dead_links read the raw lines instead,
    because a table and a link are structure and a legal block holds neither.
    """
    if path.endswith(".md"):
        return legal_blank(quieted(lines), path, ledger)
    if path.endswith(".tex"):
        return legal_blank(tex_prose(lines), path, ledger)
    # Tested before .py, because a build file can be named or extended and a .py never is.
    if build_file(path):
        return legal_blank(quieted(comment_prose(lines)), path, ledger)

    retained = []
    in_block = False
    for line in lines:
        stripped = line.strip()
        if path.endswith(".py"):
            # Count the markers on the line instead of testing how it starts and ends. The earlier
            # form closed a block by testing that the line was longer than five characters. A
            # closing triple quote on its own line measured three and reopened the block it was
            # closing. Every code line after the first multi-line docstring in a file was then read
            # as prose. EM_DASH = "-" then reported itself as an em dash, and a
            # `for row in summary` reported itself as the filler phrase.
            marks = stripped.count('"""') + stripped.count("'''")
            if marks:
                retained.append(line)
                # An odd count opens or closes. An even count is a docstring written on one line.
                if marks % 2:
                    in_block = not in_block
                continue
            retained.append(line if (in_block or stripped.startswith("#")) else "")
            continue

        # C and its headers.
        if "/*" in line:
            in_block = True
        was = in_block
        if "*/" in line:
            in_block = False
        if was or stripped.startswith("//"):
            retained.append(line)
        elif "//" in line:
            # A trailing // or ///< comment on a code line. Reading only a line-leading // leaves
            # every banned phrase in a trailing comment unflagged, and a header full of ///< briefs
            # goes unread. Retain from the first // to the end and blank the code before it, which
            # preserves the line number a finding points at. A trailing /*
            # */ block is already caught by in_block above; this is the slash form it missed. Crude
            # about a // inside a string literal, the same trade this function makes for the leading
            # case.
            cut = line.index("//")
            retained.append(line[cut:])
        else:
            retained.append("")
    return legal_blank(quieted(retained), path, ledger)
