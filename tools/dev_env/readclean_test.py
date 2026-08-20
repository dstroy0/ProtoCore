#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for readclean.py: what the blind pass removes, and what it must NOT remove.

Both halves are load-bearing. A pass that removes too little leaves the names it was meant to take
off; a pass that removes too much leaves text nothing can be checked against - and the second is the
easier mistake to ship, because the output still looks blinded.
"""

import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import readclean as RC  # noqa: E402

FAIL = 0


def check(name, cond):
    global FAIL
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAIL += 1


def blind(text):
    return RC.Blinder(set()).whole(text)


print("the module's identity goes, and every role of it goes together")
b = blind(
    "typedef struct { const uint8_t *msg; } Sha256UpdateArgs;\n"
    "typedef struct { Sha256UpdateArgs update_args; proto_bool ok; } Sha256Vars;\n"
    "extern Sha256Vars Sha256V;\n"
    "typedef struct { void (*const update)(uint8_t *restrict work); } Sha256Ns;\n"
    "static const Sha256Ns Sha256 __attribute__((unused)) = {.update = protocore_sha256_update};\n"
)
check("the name sha256 is gone entirely", "Sha256" not in b and "sha256" not in b)
# Sha256Ns, Sha256Vars, Sha256V and Sha256 are four spellings of ONE module. Four unrelated
# generic names would hide the relationship the whole shape is built on.
stem = b.split("Vars")[0].split()[-1]
check("Ns, Vars, V and the table share one stem", all("%s%s" % (stem, s) in b for s in ("Vars", "Ns", "V")))
check("and the bare table is that stem too", ("static const %sNs %s " % (stem, stem)) in b)

print()
print("the shape's own grammar survives, or there is nothing left to check conformity against")
check("an entry still takes the borrow", "(uint8_t *restrict work)" in b)
check("the entry is named as an entry, not as a local", "(*const e1)" in b)
check("the outcome member keeps its name", "proto_bool ok;" in b)
check("the attribute is not read as a call", "__attribute__((unused))" in b)
check("an args record keeps its role suffix", "Args " in b)

print()
print("a region's cast and its offset take the same letter")
b = blind(
    "#define SHA256_OFF_CTX 0u\n"
    "#define SHA256_OFF_STATE (SHA256_OFF_CTX + 64u)\n"
    "#define SHA256_CTX(w) ((Sha256Ctx *)(void *)((w) + SHA256_OFF_CTX))\n"
    "#define SHA256_FS(w) ((uint32_t *)(void *)((w) + SHA256_OFF_STATE))\n"
    "#ifndef PROTOCORE_SHA256_H\n"
)
check("the first region is A", "_OFF_A 0u" in b)
check("the second is B", "_OFF_B (" in b)
# The one thing worth checking about a region macro is that it reads the offset it belongs to.
# Falling through to the generic macro bucket gave the cast and its offset unrelated numbers.
check("the cast for A reads OFF_A", "_A(v1) ((" in b and "+ " in b and "_OFF_A))" in b)
check("the cast for B reads OFF_B", "_B(v1) ((" in b and "_OFF_B))" in b)

print()
print("three things look like identifiers and are not")
b = blind('static_assert(A <= PROTOCORE_SHA256_BORROW, "PROTOCORE_SHA256_BORROW is short - raise it");\n')
# The message a build would print. Blinding it rewrites the diagnostic into nonsense.
check("a string literal is left alone", '"PROTOCORE_SHA256_BORROW is short - raise it"' in b)
check("but the same name in code is blinded", "PROTOCORE_SHA256_BORROW," not in b)

b = blind("#ifndef PROTOCORE_FOO_H\n#define PROTOCORE_FOO_H\n#endif\n")
check("a directive keyword is grammar", b.count("#ifndef") == 1 and b.count("#define") == 1)
check("its operand is still blinded", "PROTOCORE_FOO_H" not in b)

b = blind('#include "crypto/hash/sha256/sha256.h"\nSha256V.ok = PROTO_TRUE;\n')
check("an include path is a location, not a claim", '#include "crypto/hash/sha256/sha256.h"' in b)
check("and the name in code is still blinded", "Sha256V.ok" not in b)
check("the shape's fixed vocabulary survives", "PROTO_TRUE" in b and ".ok = " in b)

print()
print("the table is one per run, so a name crossing files stays itself")
bl = RC.Blinder(set())
h = bl.whole("extern FooVars FooV;\n")
c = bl.whole("FooVars FooV;\nvoid f(void) { FooV.ok = PROTO_TRUE; }\n")
name = h.split("extern ")[1].split("Vars")[0]
check("the header and the source agree on the name", ("%sV." % name) in c)

print()
print("what is not this project's to rename")
b = blind("size_t n = strlen(s); memcpy(dst, src, n);\n")
check("a standard function keeps its name", "strlen(" in b and "memcpy(" in b)
check("a standard type keeps its name", "size_t " in b)

print()
print("FAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
