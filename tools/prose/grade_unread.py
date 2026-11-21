"""Runs the prose gate over source types it does not read, by staging them as ones it does.

The gate reads a fixed list of extensions. Everything else in a tree is invisible to it, and
invisible reads exactly like clean: pointed at src/ it reports twelve files and exits zero while
never opening the thirty-eight .cpp and ten .cu files beside them, which are the majority of the tree.

CUDA and C++ carry C comment syntax. Nothing has to be transformed. A file is copied under a
checked extension, the gate is run on the copy, and the findings are relabeled with the real path.
Line numbers are already right because nothing is rewritten.

    python utils/maint/prose/grade_unread.py                 # every unread type under src/
    python utils/maint/prose/grade_unread.py path ...        # named files or directories
    python utils/maint/prose/grade_unread.py --strict        # every note becomes breaking

WHAT THIS IS NOT

It is not a second gate and it holds no opinion about prose. Whatever the gate says about a staged
file is what it would say about the original, and the only thing added here is the staging. When the
gate's own extension list grows to cover a type, that type should be dropped from UNREAD below
instead of graded twice.
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
CHECK = os.path.join(HERE, "utils", "maint", "prose", "docs_check")

# Types the gate does not read, and the checked type each is staged as. C++ and CUDA are staged as
# .c because their comment syntax is identical; nothing about the code is being compiled here.
UNREAD = {".cu": ".c", ".cuh": ".h", ".cpp": ".c", ".cc": ".c", ".hpp": ".h", ".ps1": ".py"}


def reads():
    """The gate's own extension list, read from the gate so this cannot claim a stale gap."""
    try:
        with io.open(CHECK, encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("CHECKED"):
                    return [one.strip().strip("\"'") for one in
                            line.split("=", 1)[1].strip(" ()\n").split(",") if one.strip()]
    except OSError:
        pass
    return []


def gather(targets, known):
    """Every file under the targets whose type the gate will not read."""
    out = []
    for one in targets:
        if os.path.isfile(one):
            if os.path.splitext(one)[1] in UNREAD:
                out.append(one)
            continue
        for here, dirs, names in os.walk(one):
            dirs[:] = [d for d in dirs if d not in (".git", "__pycache__", "build", "vendor")]
            for name in sorted(names):
                type = os.path.splitext(name)[1]
                if type in UNREAD and type not in known:
                    out.append(os.path.join(here, name))
    return out


def main():
    argv = [one for one in sys.argv[1:] if not one.startswith("-")]
    strict = "--strict" in sys.argv
    known = reads()

    targets = argv or [os.path.join(HERE, "src")]
    files = gather(targets, known)
    if not files:
        print("nothing to stage: every type under %s is already read by the gate"
              % ", ".join(targets))
        return 0

    if not os.path.exists(CHECK):
        sys.stderr.write("no docs_check at %s\n" % CHECK)
        return 1

    room = tempfile.mkdtemp(prefix="grade_unread_")
    made = {}
    for path in files:
        base, type = os.path.splitext(os.path.basename(path))
        # The staged name retains the original type in it. Two files that differ only by extension
        # do not collide and the mapping back is unambiguous.
        are_in = os.path.join(room, "%s__%s%s" % (base, type.lstrip("."), UNREAD[type]))
        shutil.copyfile(path, are_in)
        made[os.path.normcase(are_in)] = path

    order = sorted(made)
    call = [sys.executable, CHECK] + order + (["--strict"] if strict else [])
    done = subprocess.run(call, capture_output=True, text=True)

    for line in (done.stdout + done.stderr).split("\n"):
        for are_in, real in made.items():
            if are_in in os.path.normcase(line):
                at = os.path.normcase(line).index(are_in)
                line = line[:at] + os.path.relpath(real, HERE) + line[at + len(are_in):]
                break
        if line.strip():
            print(line)

    print("")
    print("%d file(s) staged from types the gate does not read" % len(files))
    print("  the gate reads %s" % ", ".join(known) if known else "  gate list unknown")
    return done.returncode


if __name__ == "__main__":
    sys.exit(main())
