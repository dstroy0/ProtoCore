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

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src"

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
    "ssh_pkt",  # ssh_packet.cpp   - per-conn packet state
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


def classify(name: str, type_str: str) -> bool:
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
    last_type = type_str.replace("static", "").strip().split()
    if last_type and last_type[-1].rstrip("*&").endswith(("_ctx", "Ctx")):
        return True
    return False


def main() -> int:
    violations = []
    for cpp in sorted(SRC.rglob("*.cpp")):
        for i, line in enumerate(cpp.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            d = is_definition_line(line)
            if not d:
                continue
            name, type_str = d
            if not classify(name, type_str):
                violations.append((cpp.relative_to(ROOT), i, name, line.strip()))

    if violations:
        print("Owner-context guard: loose file-scope mutable(s) outside an owned *_ctx:\n")
        for path, ln, name, text in violations:
            print(f"  {path}:{ln}: `{name}`  ->  {text}")
        print(
            f"\n{len(violations)} violation(s). Move each into its subsystem's owned <name>_ctx "
            "(see src/services/iot/coap/coap.cpp), or, if it is genuinely the shared cross-TU "
            "substrate, add it to SHARED_SUBSTRATE in ci_tooling/check/check_owned_context.py."
        )
        return 1
    print("Owner-context guard: OK - every file-scope mutable lives in an owned _ctx.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
