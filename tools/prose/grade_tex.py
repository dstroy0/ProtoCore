"""Runs the prose gate over LaTeX, which the gate itself does not read.

docs_check checks .md, .py, .c and .h. Pointed at this tree's theory/ it reports six files and
exits clean while skipping fifteen .tex files carrying 11,409 words, the majority of the prose in
the repository. That is not the checker being wrong; it is the checker being pointed at a format it
never claimed. The count at the foot of the run is the only signal, and a number that looks like an
answer is the failure this whole class shares.

So the LaTeX is reduced to plain text here and the gate is run on that.

    python utils/maint/prose/grade_tex.py                 # every .tex under theory/
    python utils/maint/prose/grade_tex.py path.tex ...    # named files
    python utils/maint/prose/grade_tex.py --strict        # every note becomes breaking

WHY ONE OUTPUT LINE PER INPUT LINE

Every finding the gate reports carries a line number, and a number that points into a stripped
temporary file is useless to whoever has to fix it. So the reduction is line by line and never
reflows: line 40 of the plain text is line 40 of the .tex, and the findings are rewritten to name
the real file. A stripper that joined paragraphs would read better and report nothing anyone could
act on.

WHAT THIS DELIBERATELY DOES NOT DO

It does not parse LaTeX. Macros are stripped by pattern, and the argument of a text-bearing command
is retained while the command itself goes. Maths comes out entirely: the gate measures English and an
equation is not English. Where a construct is not recognized the text is left in place and never
dropped, and a hit may therefore land on something that was never prose. That is the right way to be
wrong in: a false finding is read and dismissed, a dropped paragraph is never graded at all.
"""

import io
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CHECK = os.path.join(HERE, "utils", "maint", "prose", "docs_check")
THEORY = os.path.join(HERE, "theory")

# Commands whose braced argument is prose and belongs in the graded text.
RETAIN_ARGUMENT = (
    "emph", "textbf", "textit", "texttt", "chapter", "section", "subsection", "subsubsection",
    "paragraph", "caption", "title", "author", "footnote", "textsc", "underline", "text",
)

# Environments that hold no English.
DROP_ENVIRONMENT = (
    "equation", "equation*", "align", "align*", "gather", "gather*", "displaymath",
    "tabular", "array", "verbatim", "lstlisting", "tikzpicture", "matrix", "bmatrix", "pmatrix",
)


def flatten(line):
    """One line of LaTeX reduced to the English in it."""
    # A comment starts at an unescaped percent.
    line = re.sub(r"(?<!\\)%.*$", "", line)

    # Inline and display maths.
    line = re.sub(r"\$\$.*?\$\$", " ", line)
    line = re.sub(r"(?<!\\)\$.*?(?<!\\)\$", " ", line)
    line = re.sub(r"\\\(.*?\\\)", " ", line)
    line = re.sub(r"\\\[.*?\\\]", " ", line)

    # References and citations carry keys instead of prose.
    line = re.sub(r"\\(label|ref|eqref|cite|citep|autoref|pageref|input|include|usepackage"
                  r"|bibliography|bibliographystyle|hypersetup|newcommand|renewcommand"
                  r"|DeclareMathOperator|index)\s*(\[[^\]]*\])?\s*(\{[^{}]*\})*", " ", line)

    # Text-bearing commands: retain the argument, drop the command. Repeated because arguments nest.
    for _ in range(4):
        line = re.sub(r"\\(" + "|".join(RETAIN_ARGUMENT) + r")\s*\*?\s*(\[[^\]]*\])?\s*\{([^{}]*)\}",
                      r"\3", line)

    # Any remaining command, with its optional argument. The braced argument is left in place: it is
    # more often prose than not, and leaving it risks a false finding while dropping it risks
    # grading nothing.
    line = re.sub(r"\\[a-zA-Z@]+\s*\*?\s*(\[[^\]]*\])?", " ", line)
    line = re.sub(r"\\[^a-zA-Z]", " ", line)

    line = line.replace("{", " ").replace("}", " ").replace("~", " ")
    line = re.sub(r"[ \t]+", " ", line)
    return line.rstrip()


def reads_tex():
    """Which extensions the gate itself reads, taken from the gate and never remembered.

    This tool was written because the checker read .md .py .c .h and skipped every .tex in the tree.
    That has since been fixed upstream, and a message here still claiming otherwise would be exactly
    the stale-but-plausible statement this whole exercise is about. So the answer is read out of the
    checker's own source at run time and is right whichever way it changes next.
    """
    try:
        with io.open(CHECK, encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("CHECKED"):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return "unknown"


def reduce_file(path):
    """The file as plain text, one output line per input line, and how many words survived."""
    with io.open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().split("\n")

    out = []
    dropping = None
    for line in lines:
        opened = re.search(r"\\begin\{([^}]*)\}", line)
        if dropping is None and opened and opened.group(1) in DROP_ENVIRONMENT:
            dropping = opened.group(1)
            out.append("")
            continue
        if dropping is not None:
            if re.search(r"\\end\{" + re.escape(dropping) + r"\}", line):
                dropping = None
            out.append("")
            continue
        out.append(flatten(line))

    words = sum(len(one.split()) for one in out)
    return "\n".join(out), words


def main():
    argv = [one for one in sys.argv[1:] if not one.startswith("-")]
    strict = "--strict" in sys.argv

    if argv:
        targets = argv
    else:
        targets = []
        for here, _, names in os.walk(THEORY):
            targets.extend(os.path.join(here, one) for one in sorted(names)
                           if one.endswith(".tex"))
    if not targets:
        sys.stderr.write("no .tex found under %s\n" % THEORY)
        return 1

    if not os.path.exists(CHECK):
        sys.stderr.write("no docs_check at %s\n" % CHECK)
        return 1

    room = tempfile.mkdtemp(prefix="grade_tex_")
    made = {}
    total_words = 0
    for path in targets:
        text, words = reduce_file(path)
        total_words += words
        # Flat names, because the checker prints the path it was given and it has to map back.
        are_in = os.path.join(room, os.path.basename(path).replace(".tex", "") + ".md")
        with io.open(are_in, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        made[os.path.normcase(are_in)] = path

    order = [os.path.join(room, os.path.basename(p).replace(".tex", "") + ".md") for p in targets]
    call = [sys.executable, CHECK] + order + (["--strict"] if strict else [])
    done = subprocess.run(call, capture_output=True, text=True)

    # Findings name the temporary file. Rewrite them to the .tex they came from; the line numbers
    # already line up because the reduction never reflows.
    for line in (done.stdout + done.stderr).split("\n"):
        for are_in, real in made.items():
            if are_in in os.path.normcase(line):
                start = os.path.normcase(line).index(are_in)
                line = line[:start] + os.path.relpath(real, HERE) + line[start + len(are_in):]
                break
        if line.strip():
            print(line)

    print("")
    print("%d .tex file(s), %d word(s) of prose graded" % (len(targets), total_words))
    if ".tex" in reads_tex():
        print("  the gate now reads .tex directly: this tool is a second opinion on the"
              " stripping")
    else:
        print("  the gate reads %s: none of this would otherwise be checked" % reads_tex())
    return done.returncode


if __name__ == "__main__":
    sys.exit(main())
