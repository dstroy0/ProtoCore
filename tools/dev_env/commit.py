"""Commit with a multi-paragraph message, without writing a file to hold it.

    commit.py [flags] "subject line" "first paragraph" "second paragraph" ...

FLAGS COME FIRST, before the subject. Everything from the subject onward is message text, taken
verbatim however it begins: a paragraph opening with `--dry` is a sentence about the flag, not the
flag. Filtering on a leading `--` anywhere in the argument list drops such a paragraph without
saying so, which this tool did to the commit that introduced it.

Each argument after the subject is one paragraph. They are wrapped and joined with blank lines, and
the whole message goes to `git commit -F -` down a PIPE, so no temporary file is created and no
heredoc is needed - which matters because a heredoc is exactly what a `-m` with embedded newlines
turns into, and this environment bans them.

    --amend          amend the previous commit instead of making a new one
    --no-signoff     leave the trailer off (default is to add the committer's own)
    --dry            print the message and the git command, and commit nothing
    --               everything after it is passed to git verbatim (paths, -a, ...)

THE TRAILER IS THE COMMITTER'S, AND ONLY THE COMMITTER'S. It is read from git config user.name and
user.email, the same source `git commit -s` uses - never from a flag, an environment variable, or
anything this file could be told to write. There is no way to name a different author through it,
which is the point: a commit's attribution is the person running it.

A line that is indented, or that starts a list or a table, is left exactly as written. Only prose
is re-wrapped, so a command example keeps its shape and a wrapped `git commit ...` does not become
two broken halves.
"""

import os, re, subprocess, sys, textwrap

WIDTH = 88  # the body's wrap column; the subject is never wrapped
LITERAL = re.compile(r"^(?:\s|[-*+]\s|\d+[.)]\s|\||>)")


def committer():
    """(handle, email) from git config - the same place `git commit -s` reads its trailer from.

    A parenthetical in user.name is dropped: this repo's is `dstroy0 (Douglas Quigg)`, and the
    trailer every commit here carries is the handle alone. Stripping it keeps git config the ONE
    source of attribution while matching what is already in the log - hardcoding the handle would
    give this file a second source, which is the thing the module docstring rules out.
    """
    out = []
    for key in ("user.name", "user.email"):
        r = subprocess.run(["git", "config", "--get", key], capture_output=True, text=True)
        out.append(r.stdout.strip())
    return re.sub(r"\s*\(.*\)\s*$", "", out[0]), out[1]


def wrap(para):
    """One paragraph, wrapped - unless its shape is load bearing, in which case it is left alone.

    A run of lines is emitted verbatim when ANY of its lines is indented or starts a list, table or
    quote marker: re-flowing a block that mixes prose and an indented example moves the example's
    text into the prose and loses the indent that made it an example.
    """
    lines = para.splitlines() or [para]
    if any(LITERAL.match(ln) for ln in lines if ln.strip()):
        return "\n".join(ln.rstrip() for ln in lines)
    return textwrap.fill(" ".join(para.split()), width=WIDTH, break_long_words=False, break_on_hyphens=False)


def build(subject, paragraphs, signoff=True):
    body = [subject.strip()]
    for p in paragraphs:
        body.append(wrap(p))
    text = "\n\n".join(body).rstrip() + "\n"
    if signoff:
        name, email = committer()
        if not name or not email:
            print("commit.py: git config user.name / user.email are not both set", file=sys.stderr)
            return None
        trailer = "Signed-off-by: %s <%s>" % (name, email)
        if trailer not in text:
            text += "\n" + trailer + "\n"
    return text


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")

    argv = sys.argv[1:]
    passthrough = []
    if "--" in argv:
        i = argv.index("--")
        argv, passthrough = argv[:i], argv[i + 1 :]

    # FLAGS COME FIRST, AND ONLY FIRST. Everything from the subject onward is message text, taken
    # verbatim however it begins - because a paragraph opening with `--dry` is a sentence about the
    # flag, not the flag, and filtering on a leading `--` anywhere in the list drops it silently.
    # This tool ate exactly that paragraph out of the commit that introduced it.
    flags, rest = set(), list(argv)
    while rest and rest[0].startswith("--"):
        flags.add(rest.pop(0))
    unknown = flags - {"--amend", "--no-signoff", "--dry"}
    if unknown:
        print("commit.py: unknown flag(s): %s" % ", ".join(sorted(unknown)), file=sys.stderr)
        return 2
    parts = rest
    if not parts:
        print(__doc__)
        return 2

    text = build(parts[0], parts[1:], signoff="--no-signoff" not in flags)
    if text is None:
        return 1

    cmd = ["git", "commit", "-F", "-"] + (["--amend"] if "--amend" in flags else []) + passthrough
    if "--dry" in flags:
        print("$ " + " ".join(cmd) + "   (message on stdin)\n")
        print(text)
        return 0

    # The message goes down the pipe, so it never touches the filesystem and never has to survive
    # a shell's quoting on the way.
    r = subprocess.run(cmd, input=text, text=True, encoding="utf-8")
    if r.returncode != 0:
        return r.returncode
    subprocess.run(["git", "--no-pager", "log", "-1", "--format=%h %s"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
