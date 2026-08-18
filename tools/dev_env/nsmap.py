"""Rewrite the flat call sites of an ALREADY-golden module onto its namespace, from a stated map.

goldenize.py converts a module and its call sites in one pass. A module converted in an earlier pass
leaves call sites behind - in a suite that did not build, in a bench, in another module - and by then
the flat declarations are gone, so goldenize has nothing to scan. This takes the map instead.

goldenize's own rewriter stages into `<entry>_args.<param>`, which only fits a module whose argument
groups are named after its entries. A module that groups by concern (identity, bind, msg, dgram)
states the member path per parameter here.

    nsmap.py <map.json> [path ...]        rewrite; paths default to the map's "files"
    nsmap.py <map.json> --dry [path ...]  print what it would do, and write nothing

The map:

    {
      "object": "Coap",
      "ctx": "Coap.internal",
      "files": ["test/unit/protocols/transport/test_coap/test_coap.c"],
      "entries": {
        "protocore_coap_server_reset":  {"entry": "reset", "members": [], "result": "ok"},
        "protocore_coap_server_process": {"entry": "process", "result": "n",
          "members": ["msg.req", "msg.req_len", "msg.resp", "msg.resp_cap"]}
      }
    }

An entry may also state `"seed": {"member.path": "<literal>"}`, written before the mapped arguments.
A flat name that carried a constant its longer sibling took as a parameter - count against
count_sampled - needs it, because the namespace keeps what the previous call left in that member.

Placement goes through nsconv, so it is literal-aware, hoists to the statement rather than the line,
and refuses what it cannot rewrite faithfully rather than guessing: a loop condition, a call inside a
macro that re-evaluates its argument, the right operand of && or ||, and two calls to one namespace
in one statement. A call whose argument count does not match the map is reported, not rewritten -
that is the signature having changed shape (an out-parameter that became a result member), which is
a hand conversion.
"""

import io
import json
import os
import re
import sys

import nsconv as N
from codemask import code_mask

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def call_pattern(names):
    """One pattern over the map's keys, capturing the name.

    A key may be a flat function (`protocore_coap_server_reset`) or one already spelled through its
    namespace but still handed the old argument list (`UdpListener.sendto`) - a rename that stopped
    half way. The flat form must not match the tail of a member access, so it keeps the `.`
    lookbehind; the dotted form is a member access, so it cannot.
    """
    flat = [re.escape(k) for k in names if "." not in k]
    dotted = [re.escape(k) for k in names if "." in k]
    alts = []
    if flat:
        alts.append(r"(?<![\w.>])(?:%s)" % "|".join(flat))
    if dotted:
        alts.append(r"(?<![\w>])(?:%s)" % "|".join(dotted))
    return re.compile(r"(%s)\s*\(" % "|".join(alts))


def convert(path, spec, dry):
    """Rewrite one file. Returns (rewritten, [(line, why)])."""
    obj = spec["object"]
    ctx = spec.get("ctx", obj + ".internal")
    byname = spec["entries"]
    pat = call_pattern(byname)

    s = io.open(path, encoding="utf-8").read()
    if not pat.search(s):
        return 0, []
    at, n, skipped = 0, 0, []
    mask = code_mask(s)
    while True:
        m = pat.search(s, at)
        if not m:
            break
        if not mask[m.start()]:
            at = m.end()  # named in a comment or a literal: prose, not a call
            continue
        e = byname[m.group(1)]
        end = N.close_paren(s, m.end())
        if re.match(r"\s*\{", s[end:]):
            skipped.append((s[: m.start()].count("\n") + 1, "definition, not a call"))
            at = m.end()
            continue
        args = [a.strip() for a in N.split_args(s[m.end() : end - 1])]
        args = [a for a in args if a]
        members = e.get("members", [])
        # A dotted key names the call in its converted form too, and a rewrite restarts the scan, so
        # the tool meets its own output. One argument, and it is the ctx: already done.
        if len(args) == 1 and args[0] == ctx:
            at = m.end()
            continue
        if len(args) != len(members):
            skipped.append(
                (s[: m.start()].count("\n") + 1, "takes %d args, the map states %d" % (len(args), len(members)))
            )
            at = m.end()
            continue
        # Seeds first, so an argument naming the same member overwrites the constant rather than
        # the other way round.
        staging = ["%s.%s = %s;" % (obj, mem, val) for mem, val in sorted(e.get("seed", {}).items())]
        staging += ["%s.%s = %s;" % (obj, mem, val) for mem, val in zip(members, args)]
        staging.append("%s.%s(%s);" % (obj, e["entry"], ctx))
        try:
            s = N.rewrite(s, m.start(), end, staging, "%s.%s" % (obj, e.get("result", "ok")), pat, mask)
            mask = code_mask(s)
            n += 1
            at = 0
        except ValueError as ex:
            skipped.append((s[: m.start()].count("\n") + 1, str(ex)))
            at = m.end()
    if n and not dry:
        io.open(path, "w", encoding="utf-8", newline="").write(s)
    # A rewrite restarts the scan, so every call already refused is met again on each pass. Report
    # each one once.
    return n, sorted(set(skipped))


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    dry = "--dry" in argv
    argv = [a for a in argv if a != "--dry"]
    spec = json.load(io.open(argv[0], encoding="utf-8"))
    files = argv[1:] or spec.get("files", [])
    total, left = 0, 0
    for f in files:
        p = f if os.path.isabs(f) else os.path.join(R, f.replace("/", os.sep))
        n, skipped = convert(p, spec, dry)
        total += n
        left += len(skipped)
        print("%s: %d rewritten%s" % (f, n, " (DRY, nothing written)" if dry else ""))
        for ln, why in skipped:
            print("   BY HAND %s:%d  %s" % (f, ln, why))
    print("total: %d rewritten, %d for hand conversion" % (total, left))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
