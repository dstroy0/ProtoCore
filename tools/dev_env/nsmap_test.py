"""Self-test for nsmap.py: what its call pattern matches, and the two things it must not do twice.

Each case here is a defect the tool showed in its first run: a dotted key met its own output on the
next pass and was reported as an arity mismatch, and every call it had already refused was reported
again on every restart, so one loop condition printed seventeen times.
"""

import io
import os
import sys
import tempfile

sys.path.insert(0, __file__.rsplit("nsmap_test.py", 1)[0])
from nsmap import call_pattern, convert  # noqa: E402

FAIL = 0


def check(name, got, want):
    global FAIL
    ok = got == want
    print(("  ok   " if ok else "  FAIL ") + name)
    if not ok:
        print("         got  %r" % (got,))
        print("         want %r" % (want,))
        FAIL += 1


def run(src, spec):
    """Convert a throwaway file and return (text, rewritten, skipped)."""
    fd, p = tempfile.mkstemp(suffix=".c")
    os.close(fd)
    io.open(p, "w", encoding="utf-8", newline="").write(src)
    try:
        n, skipped = convert(p, spec, dry=False)
        return io.open(p, encoding="utf-8").read(), n, skipped
    finally:
        os.remove(p)


FLAT = {
    "object": "Coap",
    "ctx": "Coap.internal",
    "entries": {"protocore_coap_reset": {"entry": "reset", "members": [], "result": "ok"}},
}

DOTTED = {
    "object": "UdpListener",
    "ctx": "UdpListener.internal",
    "entries": {"UdpListener.poll": {"entry": "poll", "members": [], "result": "ok"}},
}

print("call_pattern: a flat key is not the tail of a member access")
check(
    "the flat key matches on its own",
    call_pattern(["protocore_coap_reset"]).search("    protocore_coap_reset();") is not None,
    True,
)
check(
    "the flat key does not match Other.protocore_coap_reset",
    call_pattern(["protocore_coap_reset"]).search("    Other.protocore_coap_reset();") is not None,
    False,
)
check(
    "a dotted key matches the member access it names",
    call_pattern(["UdpListener.poll"]).search("    UdpListener.poll();") is not None,
    True,
)

print("convert: a dotted key does not reconvert its own output")
out, n, skipped = run("void f(void)\n{\n    UdpListener.poll();\n    UdpListener.poll();\n}\n", DOTTED)
check("both calls convert", n, 2)
check("nothing is left for hand conversion", skipped, [])
check(
    "the output is the converted form, once",
    out,
    "void f(void)\n{\n    UdpListener.poll(UdpListener.internal);\n    UdpListener.poll(UdpListener.internal);\n}\n",
)

print("convert: a refusal is reported once, not once per restart")
src = (
    "void f(void)\n{\n"
    "    protocore_coap_reset();\n"
    "    while (protocore_coap_reset())\n    {\n        break;\n    }\n"
    "    protocore_coap_reset();\n"
    "}\n"
)
out, n, skipped = run(src, FLAT)
check("the two plain calls convert", n, 2)
check("the loop condition is refused once", len(skipped), 1)
check("and it names the line it is on", skipped[0][0], 4)

print("convert: an argument count the map does not state is reported, not guessed")
out, n, skipped = run("void f(void)\n{\n    protocore_coap_reset(1, 2);\n}\n", FLAT)
check("nothing was rewritten", n, 0)
check("and it says what it saw", skipped, [(3, "takes 2 args, the map states 0")])

print("\nFAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
