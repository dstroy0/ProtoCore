#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Owner-context guard: fail if a library .cpp carries a loose file-scope mutable.

The library's security model (least privilege / object-capability) is that every
subsystem's mutable state lives in ONE owned, feature-gated context struct with internal
linkage (`static <name>_ctx s_x;` or the same inside an anonymous namespace), threaded
explicitly through the call graph. The only ambient symbols allowed at file scope are:

  * the single rooted owner instance(s)  -> a variable whose TYPE ends in `_ctx`
  * immutable data                       -> `const` / `constexpr`
  * functions, structs/enums, typedefs, using, templates, externs
  * the documented cross-TU shared substrate (the protocol-homogeneous pools), allow-listed

Anything else at file scope (a loose `static int s_foo;`, a scattered `Foo g_bar[N];`) is an
outlier and fails this check. Uniformity is the point: it is how the outliers get caught.

Heuristic, not a full C++ parser: file-scope definitions in this codebase sit at column 0
(whether `static` or inside an anonymous `namespace {`); struct members and function bodies
are indented, so a column-0 anchor separates them cleanly.
"""

import os
import re
import sys
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import baseline as bl  # noqa: E402  (path set above)

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src"

# The linkage half of the rule is ratcheted, not absolute: it was never enforced, so the tree
# carries contexts that are named right and linked wrong. Recorded so the count can only fall -
# same contract as the ban 19 / 20 sweeps. The struct half stays absolute and fails on sight.
BASELINE = bl.path_for(__file__, "owned_context_baseline")

# Cross-TU shared substrate: intentionally external-linkage, indexed by every ProtoHandler
# through the L5 seam (see docs/ARCHITECTURE.md). These are the documented "one seam", not
# scattered outliers, so they are exempt. Keyed by the exact variable name.
SHARED_SUBSTRATE = {
    "conn_pool",  # tcp.cpp   - the TCP connection pool (extern in tcp.h)
    "http_pool",  # http_parser.cpp - the parsed-request pool (extern in http_parser.h)
    "ws_pool",  # websocket.cpp   - the WebSocket connection pool (extern in websocket.h)
    "pc_sse_pool",  # sse.cpp         - the SSE connection pool (extern in sse.h)
    "listener_pool",  # listener.cpp    - the listener pool
    "http_req_count",  # presentation.cpp - per-slot request counter (extern)
    # SSH per-connection substrate: one row per SSH slot, indexed cross-TU by the SSH layers.
    "ssh_chan",  # ssh_channel.cpp  - the SSH channel table
    "ssh_keys",  # ssh_keymat.cpp   - per-conn key material
    "ssh_dh",  # ssh_keymat.cpp   - per-conn DH state
    "ssh_pkt",  # ssh_packet_state.cpp - per-conn packet state
    "ssh_sess",  # ssh_transport.cpp- per-conn session state
    "ssh_host_pubkey",  # ssh_rsa.cpp      - the loaded RSA host public key
    "crypto_work",  # ssh_bignum.cpp   - shared SSH bignum scratch
    "pc_ap_ip",  # tcp.cpp    - the softAP IP (extern)
}

# A file-scope definition line (column 0). Optional ALL_CAPS attribute macros
# (EXT_RAM_BSS_ATTR, PC_*_ATTR, ...) may bracket `static`.
ATTR = r"(?:[A-Z_][A-Z0-9_]*\s+)*"
DEF_RE = re.compile(
    r"^(?P<pre>" + ATTR + r"static\s+" + ATTR + r"|" + ATTR + r")"
    r"(?P<decl>[A-Za-z_][\w:<>,*&\s]*?)"  # type + declarator
    r"(?P<name>[A-Za-z_]\w*)"  # the variable name
    r"\s*(?:\[[^\]]*\])*\s*"  # optional array extents
    r"(?:=|;|\{)"  # initializer / end / brace-init
)

SKIP_FIRST = (
    "return",
    "if",
    "else",
    "for",
    "while",
    "do",
    "switch",
    "case",
    "goto",
    "namespace",
    "using",
    "typedef",
    "template",
    "extern",
    "friend",
    "public",
    "private",
    "protected",
    "struct",
    "class",
    "enum",
    "union",
    "typename",
)


def is_definition_line(line: str):
    """Return (name, type_str) for a column-0 mutable variable definition, else None."""
    if not line or line[0].isspace():
        return None
    s = line.rstrip("\n")
    stripped = s.strip()
    if not stripped or stripped.startswith(("//", "/*", "*", "#", "}", ")", "@")):
        return None
    first = re.match(r"[A-Za-z_]\w*", stripped)
    if first and first.group(0) in SKIP_FIRST:
        return None
    # A `(` before the terminator means a function decl/def (or direct-init, which this
    # codebase does not use for file-scope mutables) - not a plain variable.
    head = re.split(r"[;={]", stripped, maxsplit=1)[0]
    if "(" in head:
        return None
    m = DEF_RE.match(s)
    if not m:
        return None
    decl = (m.group("pre") + m.group("decl")).strip()
    return m.group("name"), decl


def is_ctx_type(type_str: str) -> bool:
    """True if the declared type is an owned-context type (`*_ctx` / `*Ctx`)."""
    last_type = type_str.replace("static", "").strip().split()
    return bool(last_type) and last_type[-1].rstrip("*&").endswith(("_ctx", "Ctx"))


def classify(name: str, type_str: str, in_anon_ns: bool) -> bool:
    """True if this file-scope definition is allowed (owner / const / substrate / seam)."""
    if name in SHARED_SUBSTRATE:
        return True
    if name.startswith("_test"):  # external test-vector seam (accessed from test/)
        return True
    if "::" in type_str:  # an out-of-line CLASS static-member definition - the class owns it
        return True
    if re.search(r"\b(const|constexpr|thread_local)\b", type_str):  # immutable / per-thread, not ambient
        return True
    # The single rooted owner: its type ends in `_ctx` (pc_coap_ctx, pc_client_ctx, ...).
    # `Ctx` is also accepted so a stray PascalCase holdout still passes rather than
    # reading as a loose global; docs/SYMBOLS.md makes `pc_snake_case` the law.
    #
    # Naming the type `*Ctx` is NOT what makes state owned - INTERNAL LINKAGE is. A context with
    # external linkage is reachable by `extern` from any TU, which is the condition the owner rule
    # exists to prevent, and it reads as compliant because the type name looks right. `SendCtx
    # s_send;` in protocore.cpp was exactly that: two unrelated per-slot arrays in one struct,
    # defined in a TU that used neither, `extern`'d into a shared header and reached by two others.
    if is_ctx_type(type_str):
        return bool(re.search(r"\bstatic\b", type_str)) or in_anon_ns
    return False


# `extern <Something>Ctx name;` in a header - the declaration that makes a context reachable from
# another TU. The definition-side check cannot see this on its own, and it is the half that does the
# damage: it is what turns an owned context into shared state.
EXTERN_CTX_RE = re.compile(r"^\s*extern\s+(?P<type>[A-Za-z_]\w*)\s+(?P<name>[A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;")


def scan_extern_ctx():
    """Headers must not publish a context type by `extern` - that is shared state, not an owner."""
    out = []
    for hdr in sorted(SRC.rglob("*.h")):
        for i, line in enumerate(hdr.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            m = EXTERN_CTX_RE.match(line)
            if not m or m.group("name") in SHARED_SUBSTRATE:
                continue
            if is_ctx_type(m.group("type")):
                out.append((hdr.relative_to(ROOT), i, m.group("name"), line.strip()))
    return out


def main(argv) -> int:
    loose = []     # not a context at all - absolute, fails on sight
    linkage = []   # a context, but external linkage - ratcheted
    for cpp in sorted(SRC.rglob("*.cpp")):
        anon_depth = -1  # brace depth at which an anonymous namespace opened, or -1
        depth = 0
        for i, line in enumerate(cpp.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if anon_depth < 0 and re.match(r"^namespace\s*\{", line):
                anon_depth = depth
            d = is_definition_line(line)
            if d:
                name, type_str = d
                if not classify(name, type_str, anon_depth >= 0):
                    rel = str(cpp.relative_to(ROOT)).replace(os.sep, "/")
                    # A context named right but linked wrong is the ratcheted half; anything that is
                    # not a context at all was already failing before and stays absolute.
                    (linkage if is_ctx_type(type_str) else loose).append((rel, i, name, line.strip()))
            depth += line.count("{") - line.count("}")
            if anon_depth >= 0 and depth <= anon_depth:
                anon_depth = -1

    externs = [(str(p).replace(os.sep, "/"), i, n, t) for p, i, n, t in scan_extern_ctx()]
    ratcheted = linkage + externs

    # Key by file + name + OCCURRENCE ORDINAL. Without the ordinal, config_store.cpp's two `s_cfg`
    # definitions collapse to one key and a re-added third would pass unseen - the same trap
    # check_src_banned records from ecdsa.cpp's three `t[8]`. Ordinals keep the recorded set
    # prefix-closed under removal, so the count falls and never rises.
    seen = {}
    keys = {}
    for v in ratcheted:
        base = f"{v[0]}|{v[2]}"
        seen[base] = seen.get(base, 0) + 1
        keys[v] = f"{base}#{seen[base]}"

    if "--baseline" in argv:
        n = bl.save(BASELINE, (keys[v] for v in ratcheted))
        print(f"check_owned_context: recorded {n} known linkage site(s) as the floor")
        return 0

    new_ratcheted, known, fixed = bl.filter_new(ratcheted, lambda v: keys[v], BASELINE)
    violations = loose + [v for v in new_ratcheted if v in linkage]
    externs = [v for v in new_ratcheted if v in externs]

    if violations or externs:
        if violations:
            print("Owner-context guard: file-scope mutable(s) that no TU owns:\n")
            for path, ln, name, text in violations:
                print(f"  {path}:{ln}: `{name}`  ->  {text}")
            print(
                f"\n{len(violations)} violation(s). Move each into its subsystem's owned <name>_ctx "
                "(see src/services/iot/coap/coap.cpp), give the context internal linkage (`static`, "
                "or an anonymous namespace), or, if it is genuinely the shared cross-TU substrate, "
                "add it to SHARED_SUBSTRATE in ci_tooling/check/check_owned_context.py."
            )
        if externs:
            print("\nOwner-context guard: context type(s) published by `extern` in a header:\n")
            for path, ln, name, text in externs:
                print(f"  {path}:{ln}: `{name}`  ->  {text}")
            print(
                f"\n{len(externs)} violation(s). A context reachable by `extern` is shared state, "
                "not owned state - naming the type `*Ctx` does not make it owned, internal linkage "
                "does. Keep the definition private to its TU and expose what callers need as "
                "functions (see pc_resp_holds_slot / pc_file_holds_slot)."
            )
        return 1
    tail = f" ({known} known linkage site(s) remain{f', {fixed} fixed' if fixed else ''})" if known else ""
    print(f"Owner-context guard: OK - every file-scope mutable lives in an owned _ctx{tail}.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
