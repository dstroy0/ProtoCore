"""Self-test for shapeaudit.py: the reader names constructs, and the checks answer about them.

The load-bearing one is `selfcheck`. Every check reads the golden's own answer as its reference, so
a check the golden fails is a check that marks all 400 modules divergent on a question the target of
the conversion does not itself satisfy - and it fails silently, as a bigger number.
"""

import os, sys

sys.path.insert(0, __file__.rsplit("shapeaudit_test.py", 1)[0])
import shapeaudit as S  # noqa: E402

FAIL = 0


def check(name, cond):
    global FAIL
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAIL += 1


def design(text, suffix=".c"):
    """Parse a snippet by writing it where the reader can read it."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "__shapeaudit_probe" + suffix)
    with open(p, "w", encoding="utf-8") as f:
        f.write(text)
    try:
        return S.read_design(p)
    finally:
        os.remove(p)


def one(design_, kind):
    return [d for d in design_ if d["kind"] == kind]


print("the golden answers yes to every check")
gid, gh, gc = S.golden_designs()
for name, ans, _ in S.answers(gh, gc, gh, gc, gid):
    check(name, ans is True)
print()

print("identity is read off the pair, not guessed")
check("module", gid["module"] == "sha256")
check("object", gid["object"] == "Sha256")
check("namespace type", gid["ns"] == "Sha256Ns")
check("gate", gid["gate"] == "PROTOCORE_ENABLE_SHA256")
check("borrow", gid["borrow"] == "PROTOCORE_SHA256_BORROW")
print()

print("substitution rewrites the longest token first")
out = S.substitute(
    "PROTOCORE_SHA256_BORROW Sha256Ns Sha256 sha256",
    gid,
    {"module": "dnp3", "object": "Dnp3", "ns": "Dnp3Ns", "gate": "G", "guard": "H", "borrow": "PROTOCORE_DNP3_BORROW"},
)
check("the borrow is not left as a tail of itself", out == "PROTOCORE_DNP3_BORROW Dnp3Ns Dnp3 dnp3")
print()

print("a function-pointer member is named from inside its parentheses")
d = design("typedef struct\n{\n    void (*const init)(uint8_t *restrict work);\n    proto_bool ok;\n} FooNs;\n", ".h")
mem = one(d, "record")[0]["members"]
check("the entry is called init, not work", mem[0]["name"] == "init")
check("it is seen as a function pointer", mem[0]["fnptr"] is True)
check("the result member is not one", mem[1] == {"name": "ok", "fnptr": False, "type": "proto_bool"})
print()

print("static_assert is not a prototype")
d = design("static_assert(A + sizeof(uint32_t) * 8 <= PROTOCORE_FOO_BORROW, \"short\");\n")
check("read as an assert", len(one(d, "static_assert")) == 1)
check("and not as a function", one(d, "prototype") == [])
check("it is seen to cite the borrow", one(d, "static_assert")[0]["cites_borrow"] is True)
print()

print("local arrays: data only, so a flash string is not pulled into RAM")
d = design(
    "static void f(void)\n{\n"
    "    uint8_t buf[64];\n"
    "    const char name[] = \"protocore\";\n"
    "    const char *const tab[3] = {\"a\", \"b\", \"c\"};\n"
    "    const uint8_t k[16] = {0};\n"
    "    uint8_t vla[n];\n"
    "}\n"
)
got = [a["name"] for a in one(d, "function")[0]["locals"]]
check("a working buffer is found", "buf" in got)
check("a const data array is found too", "k" in got)
check("a char array initialised from a string is not", "name" not in got)
check("nor is a table of pointers to literals", "tab" not in got)
check("a non-literal extent is still storage", "vla" in got)
check("and it is marked as not literal", [a for a in one(d, "function")[0]["locals"] if a["name"] == "vla"][0]["literal"] is False)
print()

print("the offset chain is read positionally")
d = design(
    "#define F_OFF_A 0u\n"
    "#define F_OFF_B (F_OFF_A + sizeof(FCtx))\n"
    "#define F_OFF_C (F_OFF_B + 64u)\n"
    "static_assert(F_OFF_C + 32u <= PROTOCORE_F_BORROW, \"short\");\n"
)
t = S.traits(d, "source")
check("first offset is zero", t["offset_first_is_zero"] is True)
check("every link holds", t["offset_chain_links"] == [True, True])
check("so the chain is intact", t["offset_chain_intact"] is True)
check("and the assert covers the last region", t["offset_assert_covers_last"] is True)

d = design(
    "#define F_OFF_A 0u\n"
    "#define F_OFF_B (F_OFF_A + sizeof(FCtx))\n"
    "#define F_OFF_C (F_OFF_A + 64u)\n"
    "static_assert(F_OFF_B + 32u <= PROTOCORE_F_BORROW, \"short\");\n"
)
t = S.traits(d, "source")
check("an offset chained past its predecessor breaks the chain", t["offset_chain_links"] == [True, False])
check("which is reported, not accepted for naming an offset", t["offset_chain_intact"] is False)
check("an assert against a middle region does not cover the last", t["offset_assert_covers_last"] is False)
print()

print("a cast region needs its offset asserted; a uint8_t* region does not")
d = design(
    "#define F_OFF_A 0u\n"
    "#define F_OFF_B (F_OFF_A + sizeof(FCtx))\n"
    "static_assert(F_OFF_A % _Alignof(FCtx) == 0, \"misaligned\");\n"
    "#define F_CTX(w) ((FCtx *)(void *)((w) + F_OFF_A))\n"
    "#define F_BUF(w) ((w) + F_OFF_B)\n"
    "#define F_ACC(w) ((uint32_t *)(void *)((w) + F_OFF_B))\n"
)
t = S.traits(d, "source")
check("the two casts are found", len(t["cast_regions"]) == 2)
check("the plain uint8_t* region is not a cast region", "F_BUF" not in " ".join(t["cast_regions"]))
check("the asserted one is covered", "F_CTX" not in " ".join(t["cast_regions_unaligned"]))
check("the unasserted one is reported", "F_ACC" in " ".join(t["cast_regions_unaligned"]))

d = design(
    "#define F_OFF_A 0u\n"
    "static_assert(F_OFF_A + 4u <= PROTOCORE_F_BORROW, \"short\");\n"
    "#define F_CTX(w) ((FCtx *)(void *)((w) + F_OFF_A))\n"
)
t = S.traits(d, "source")
check("a SIZE assert does not count as an alignment one", t["cast_regions_unaligned"] == ["F_CTX->FCtx@F_OFF_A"])
print()

print("the borrow tested for null is found wherever it is spelled")
d = design(
    "static void f(uint8_t *restrict work)\n{\n"
    "    if (!work || !Ns.args.out)\n    {\n        return;\n    }\n"
    "}\n"
    "static void g(uint8_t *restrict work)\n{\n"
    "    Ns.ok = work != NULL;\n"
    "}\n"
    "static void h(EmCtx *e)\n{\n"
    "    if (e->work == NULL)\n    {\n        return;\n    }\n"
    "}\n"
)
t = S.traits(d, "source")
found = " | ".join(x["text"] for x in d if x["kind"] == "null_borrow_test")
check("the `if (!work || ...)` disjunct is found", "!work" in found)
# euromap77 spelled it as an assignment, and an if-shaped search walked straight past it.
check("the `ok = work != NULL` assignment is found too", "work != NULL" in found)
check("a struct member called work is NOT counted", "e->work" not in found)
check("so the count is two, not three", t["null_borrow_tests"] == 2)
print()

print("a check that cannot apply is n/a, never no")
na = dict((n, a) for n, a, _ in S.answers(gh, None, gh, gc, gid))
check("a header with no .c answers nothing", all(v is None for v in na.values()))
na = dict((n, a) for n, a, _ in S.answers(gh, gc, gh, gc, dict(gid, borrow="")))
check("no borrow means the borrow questions have no subject", na["borrow carved"] is None)
check("and the offsets question with it", na["offsets chain"] is None)
check("but the namespace question still applies", na["namespace"] is True)
print()

print("FAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
