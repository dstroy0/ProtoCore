# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared helpers for the real-protocol interop peers.

Every peer module reports results through the same `Probe` so the CLI output is
uniform and the exit code is meaningful (0 = all checks passed). A peer either
*probes* the device (device is the server: HTTP / WebSocket / SNMP / CoAP /
Modbus-slave / OPC-UA-server) or *serves* a reference peer the device connects
to (device is the client: MQTT broker / Modbus master / OPC-UA client).
"""

from __future__ import annotations

import importlib
import sys
import time

# ANSI colours, disabled when stdout is not a TTY so logs stay clean.
_TTY = sys.stdout.isatty()
_GREEN = "\033[32m" if _TTY else ""
_RED = "\033[31m" if _TTY else ""
_DIM = "\033[90m" if _TTY else ""
_RST = "\033[0m" if _TTY else ""


class Probe:
    """Collects PASS/FAIL checks for one peer run and prints a summary."""

    def __init__(self, name: str):
        self.name = name
        self.passed = 0
        self.failed = 0

    def check(self, label: str, ok: bool, detail: str = "") -> bool:
        tag = f"{_GREEN}PASS{_RST}" if ok else f"{_RED}FAIL{_RST}"
        line = f"  [{tag}] {label}"
        if detail:
            line += f" {_DIM}({detail}){_RST}"
        print(line)
        if ok:
            self.passed += 1
        else:
            self.failed += 1
        return ok

    def info(self, msg: str) -> None:
        print(f"  {_DIM}{msg}{_RST}")

    def summary(self) -> bool:
        total = self.passed + self.failed
        ok = self.failed == 0
        tag = f"{_GREEN}OK{_RST}" if ok else f"{_RED}{self.failed} FAILED{_RST}"
        print(f"{self.name}: {self.passed}/{total} checks [{tag}]")
        return ok


def require(*modules: str):
    """Import the first available module name, or exit with an install hint.

    Pass alternatives in preference order, e.g. require("aiocoap"). Returns the
    imported module. A missing dependency is a setup problem, not a test failure,
    so we exit 2 (distinct from an interop FAIL, which is exit 1).
    """
    last = None
    for name in modules:
        try:
            return importlib.import_module(name)
        except ImportError as exc:  # noqa: PERF203 - tiny loop, clarity first
            last = exc
    sys.stderr.write(
        f"error: missing dependency for this peer (tried: {', '.join(modules)}).\n"
        f"       install the interop requirements:\n"
        f"           pip install -r test/servers/requirements.txt\n"
        f"       ({last})\n"
    )
    sys.exit(2)


def have(module: str) -> bool:
    """True if an optional module is importable (no exit)."""
    try:
        importlib.import_module(module)
        return True
    except ImportError:
        return False


def wait_until(predicate, timeout_s: float, interval_s: float = 0.1) -> bool:
    """Poll predicate() until true or timeout; returns the final value."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if predicate():
            return True
        time.sleep(interval_s)
    return predicate()
