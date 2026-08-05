#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fail if example code - in a sketch OR in a README - drifts from the library API.

This exists because the drift already happened and nothing noticed. The sketches
were migrated to the library transport and to `enum class`, but the READMEs were
not, and the annotated-source blocks are hand-rolled on purpose (the annotation is
the teaching content, so they cannot be generated). The result:

  * 446 vendor calls across 83 READMEs - `#include <WiFi.h>`, `WiFi.localIP()`,
    `WiFi.setSleep()` - in blocks that claimed to reproduce sketches containing
    none of them.
  * 68 bare enum-class members across 22 READMEs, which do not compile at all.
  * 7 uses of `WiFiClient`, which docs/SRCBANNED.md rule 6 bans outright.

docs/SRCBANNED.md rule 6 already said "Applies to `examples/` too" and even printed
the command to run, but check_src_banned.py scans only `src/` and never reads
markdown. A rule that names its own scope is not enforced until a checker reads
that scope - so this reads it.

Checked (in `.ino`/`.cpp` and in README fenced code alike):
  1. vendor networking calls           - the library owns the transport
  2. banned socket classes (rule 6)    - WiFiClient / WiFiUDP / AsyncUDP

Usage:  python ci_tooling/check/check_examples.py [--verbose]
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import doc_region as dr  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
EXAMPLES = os.path.join(ROOT, "examples")

# Vendor networking. The library maps every one of these onto its own call, so a
# vendor call in an example is teaching the unmapped form.
VENDOR = [
    (re.compile(r"#include\s*<WiFi\.h>"), "#include <WiFi.h>", "the library's own physical.h"),
    (re.compile(r"\bWiFi\.localIP\(\)"), "WiFi.localIP()", "pc_net_egress_ip()"),
    (re.compile(r"\bWiFi\.softAPIP\(\)"), "WiFi.softAPIP()", "pc_net_ap_ip()"),
    (re.compile(r"\bWiFi\.RSSI\(\)"), "WiFi.RSSI()", "pc_net_rssi()"),
    (re.compile(r"\bWiFi\.macAddress\b"), "WiFi.macAddress()", "pc_net_mac()"),
    (re.compile(r"\bWiFi\.SSID\(\)"), "WiFi.SSID()", "pc_net_ssid()"),
    (re.compile(r"\bWiFi\.channel\(\)"), "WiFi.channel()", "pc_net_channel()"),
    (re.compile(r"\bWiFi\.softAP\("), "WiFi.softAP()", "init_wifi_ap_physical()"),
    (re.compile(r"\bWiFi\.setSleep\("), "WiFi.setSleep()", "nothing - no sketch needs it"),
    (re.compile(r"\bWiFi\.mode\("), "WiFi.mode()", "the init_wifi_*_physical() you call"),
    (re.compile(r"\bWiFi\.disconnect\("), "WiFi.disconnect()", "init_wifi_radio_physical()"),
    (re.compile(r"\besp_wifi_\w+"), "esp_wifi_*", "the library's physical layer"),
    (re.compile(r"\besp_netif_\w+"), "esp_netif_*", "the library's physical layer"),
    # rule 6 proper: banned outright, not merely superseded
    (re.compile(r"\bWiFiClient(?:Secure)?\b"), "WiFiClient", "pc_client_* (SRCBANNED rule 6)"),
    (re.compile(r"\bWiFiUDP\b"), "WiFiUDP", "pc_udp_* (SRCBANNED rule 6)"),
    (re.compile(r"\bAsyncUDP\b"), "AsyncUDP", "pc_udp_* (SRCBANNED rule 6)"),
]

_STR = re.compile(r'"(?:[^"\\]|\\.)*"')
_COM = re.compile(r"//.*$")


def code_only(line):
    """Blank out string literals and trailing comments.

    `Serial.print("Connecting to WiFi")` is legitimate - 80 sketches print it - and
    several sketches carry the comment "no Arduino WiFi". Neither is a call.
    """
    return _COM.sub("", _STR.sub('""', line))


def blank_block_comments(text):
    """Replace /* ... */ with blank space, preserving newlines and line numbers.

    Every sketch opens with a `/** @file ... */` prose block, and that prose talks
    about protocol states ("ReadState -> RUN / STOP / CONFIG"). Read as code, those
    words look like bare enum members.
    """

    def _blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))

    return re.sub(r"/\*.*?\*/", _blank, text, flags=re.S)


# Untracked trees a local build drops inside examples/: a fetched dependency and its objects.
PRUNE = {"managed_components", "build", ".pio", "node_modules"}


def iter_example_code():
    """Yield (path, lineno, line) for every line of example CODE.

    Sketch bodies in full, and only the fenced blocks of a README.
    """
    for base, dirs, files in os.walk(EXAMPLES):
        dirs[:] = [d for d in dirs if d not in PRUNE]
        for f in sorted(files):
            p = os.path.join(base, f)
            if f.endswith((".ino", ".cpp", ".h")):
                text = blank_block_comments(open(p, encoding="utf-8", errors="replace").read())
                for n, ln in enumerate(text.split("\n"), 1):
                    yield p, n, ln
            elif f == "README.md":
                text = open(p, encoding="utf-8", errors="replace").read()
                for info, block, start in dr.fenced_code(text):
                    lang = info.split()[0].lower() if info else ""
                    if lang not in ("cpp", "c", "c++", "ino", "arduino"):
                        continue
                    for i, ln in enumerate(blank_block_comments(block).split("\n")):
                        yield p, start + 1 + i, ln


_ANY_INCLUDE = re.compile(r"^\s*#\s*include\b")
_CORE_INCLUDE = re.compile(r'^\s*#\s*include\s+"protocore\.h"')


def check_sketch_include_order():
    """`#include "protocore.h"` must be the FIRST include in every sketch.

    Arduino discovers a library by scanning the sketch's includes in order, and only then adds that
    library's src/ to the include path. Any src/-relative include placed above protocore.h is
    therefore unresolvable and the sketch does not compile.

    This is gated because it is silently easy to break in bulk: an alphabetical include sort - which
    is what clang-format does by default - moves "network_drivers/..." above "protocore.h" and takes
    out every sketch at once. It happened to 131 of 152 here, and nothing but an actual ESP32 build
    would have noticed.
    """
    out = []
    for base, dirs, files in os.walk(EXAMPLES):
        dirs[:] = [d for d in dirs if d not in PRUNE]
        for f in sorted(files):
            if not f.endswith(".ino"):
                continue
            p = os.path.join(base, f)
            rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
            incs = [
                (n, ln)
                for n, ln in enumerate(open(p, encoding="utf-8", errors="replace").read().split("\n"), 1)
                if _ANY_INCLUDE.match(ln)
            ]
            if not incs:
                continue
            if not any(_CORE_INCLUDE.match(ln) for _n, ln in incs):
                out.append((rel, incs[0][0], 'sketch has no #include "protocore.h"', incs[0][1].strip()))
            elif not _CORE_INCLUDE.match(incs[0][1]):
                out.append(
                    (
                        rel,
                        incs[0][0],
                        "protocore.h must be the FIRST include (Arduino resolves the library by "
                        "scanning includes in order; anything src/-relative above it cannot resolve)",
                        incs[0][1].strip(),
                    )
                )
    return out


def main():
    verbose = "--verbose" in sys.argv[1:]

    findings = []
    for p, n, raw in iter_example_code():
        line = code_only(raw)
        if not line.strip():
            continue
        rel = os.path.relpath(p, ROOT).replace(os.sep, "/")

        for rx, what, instead in VENDOR:
            if rx.search(line):
                findings.append((rel, n, f"vendor call {what} - use {instead}", raw.strip()))

    findings.extend(check_sketch_include_order())

    if findings:
        print(
            f"check_examples: {len(findings)} problem(s) in " f"{len({f[0] for f in findings})} file(s)\n",
            file=sys.stderr,
        )
        for rel, n, why, src in findings[: (len(findings) if verbose else 40)]:
            print(f"  {rel}:{n}: {why}", file=sys.stderr)
            print(f"      {src[:100]}", file=sys.stderr)
        if not verbose and len(findings) > 40:
            print(f"  ... {len(findings) - 40} more (--verbose for all)", file=sys.stderr)
        print("\nSee docs/SRCBANNED.md rule 6 and ci_tooling/README.md.", file=sys.stderr)
        return 1

    n_files = sum(1 for _ in os.walk(EXAMPLES))
    print(f"check_examples: OK - example code uses the library API " f"({n_files} dirs scanned).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
