"""Self-test for shapeaudit.py: the reader names constructs, and the checks answer about them.

The load-bearing one is `selfcheck`. Every check reads the golden's own answer as its reference, so
a check the golden fails is a check that marks all 400 modules divergent on a question the target of
the conversion does not itself satisfy - and it fails silently, as a bigger number.
"""

import io, os, re, sys

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

print("a call does not spell the borrow as null")
d = design(
    "static void f(uint8_t *restrict work)\n{\n"
    "    Der.read(work);\n"
    "    Der.enter(NULL);\n"
    "    Hpack.encode_header(nullptr);\n"
    "    memset(p, 0, n);\n"
    '    log_it("Der.read(NULL) used to be written here");\n'
    "}\n"
)
t = S.traits(d, "source")
# The other half of `no null-borrow test`: that one asks whether the CALLEE doubts the guarantee,
# this one asks whether the CALLER keeps it. Sixty-two of the sixty-three in the tree reached
# entries that discard the argument, so nothing crashed and nothing reported them.
check("a null borrow argument is found", t["null_borrow_args"] == 2)
check("both spellings count", t["null_borrow_calls"] == ["Der.enter(NULL)", "Hpack.encode_header(nullptr)"])
check("a real borrow is not one", "Der.read(work)" not in " ".join(t["null_borrow_calls"]))
# `memset(p, 0, n)` is a call with a literal 0 and no borrow anywhere near it.
check("an unrelated zero argument is not one", "memset" not in " ".join(t["null_borrow_calls"]))
check("and the same call inside a string is not code", t["null_borrow_args"] == 2)
print()

print("a namespace is reached in one hop, and the reader can tell which hop it is")
gsrc = io.open(S.__file__.rsplit("shapeaudit.py", 1)[0] + "../../src/crypto/hash/sha256/sha256.c", encoding="utf-8").read()


def deep(inject):
    return S.traits(design(gsrc.replace("PROTOCORE_BEGIN_DECLS", "PROTOCORE_BEGIN_DECLS\n" + inject, 1)), "source")


check("the golden reaches nothing at depth two", deep("")["deep_ns_accesses"] == 0)
# `<X>V.<entry>_args.<operand>` IS the one hop. If this counted, every converted module would
# answer no to the question and the check would say nothing about anything.
check(
    "an operand read is not a detour",
    deep("static void probe(void) { Sha256V.update_args.len = 1; }")["deep_ns_accesses"] == 0,
)
# A REAL pair: session.h declares `WorkerNs *workers;` in SessionVars. The pair has to be real,
# because whether the hop lands in another module's table is read from that declaration and not
# from the shape of the expression - which is the whole point of the check.
check("the index knows a namespace pointer when it sees one", ("SessionV", "workers") in S.ns_pointer_members())
t = deep("static void probe(void) { SessionV.workers->pump = 1; }")
check("reaching another namespace through one is a reach", t["deep_ns_accesses"] == 1)
check("and the chain is kept, because the fix is per-site", t["deep_ns_chains"] == ["SessionV.workers->pump"])
# Same expression shape, ordinary pointer operand. lower.h declares `protocore_pcb *pcb;`, and
# counting this reported nine modules for reading a field of something they point at.
check(
    "a hop through a plain pointer operand is not",
    deep("static void probe(void) { TcpLowerV.pcb->ttl = 1; }")["deep_ns_accesses"] == 0,
)
check(
    "so is a nested Vars",
    deep("static void probe(void) { Sha256V.OtherV.slot = 1; }")["deep_ns_accesses"] == 1,
)
# Every regex over this tree goes through the code mask or it reads the prose above the code.
check(
    "the same chain in a comment is prose",
    deep("// was Sha256V.workers->pump = 1;")["deep_ns_accesses"] == 0,
)
print()

print("an entry declared in the Ns struct is bound by the table")
ghsrc = io.open(S.__file__.rsplit("shapeaudit.py", 1)[0] + "../../src/crypto/hash/sha256/sha256.h", encoding="utf-8").read()
check("the golden leaves none unbound", S.traits(design(ghsrc, ".h"), "header")["unbound_entries"] == [])
victim = re.search(r"\(\*const (\w+)\)", ghsrc).group(1)
dropped = re.sub(r"\n\s*\.%s\s*=[^,]*,\n" % victim, "\n", ghsrc, count=1)
check("dropping one binding is not silent", dropped != ghsrc)
# `static const` zero-fills what the initializer omits: the miss is a NULL function pointer that
# compiles, links, and segfaults on the first call. forwarded_trust shipped one.
check(
    "an unbound entry is named, not zero-filled quietly",
    S.traits(design(dropped, ".h"), "header")["unbound_entries"] == [victim],
)
# POSITIONAL. mmgr writes all five of its tables `{fn, fn, fn}` - no `.name =` anywhere - and
# reading only designated initializers said every entry in them was unbound, which is backwards:
# they are bound, in order. Five modules were reported for a shape that is simply the other legal
# spelling.
POSITIONAL = """#ifndef P_H
#define P_H
#if PROTOCORE_ENABLE_P
PROTOCORE_BEGIN_DECLS
typedef struct { void (*const a)(uint8_t *restrict work); void (*const b)(uint8_t *restrict work); } PNs;
void p_a(uint8_t *restrict work);
void p_b(uint8_t *restrict work);
static const PNs p __attribute__((unused)) = {p_a, p_b};
PROTOCORE_END_DECLS
#endif
#endif
"""
check("a positional table binds in order", S.traits(design(POSITIONAL, ".h"), "header")["unbound_entries"] == [])
check(
    "and a short positional table leaves the tail unbound",
    S.traits(design(POSITIONAL.replace("{p_a, p_b}", "{p_a}"), ".h"), "header")["unbound_entries"] == ["b"],
)
print()

print("a check that cannot apply is n/a, never no")
na = dict((n, a) for n, a, _ in S.answers(gh, None, gh, gc, gid))
check("a header with no .c answers nothing", all(v is None for v in na.values()))
na = dict((n, a) for n, a, _ in S.answers(gh, gc, gh, gc, dict(gid, borrow="")))
check("no borrow means the borrow questions have no subject", na["borrow carved"] is None)
check("and the offsets question with it", na["offsets chain"] is None)
check("but the namespace question still applies", na["namespace"] is True)
print()

print("a check compares the golden's SHAPE, not its SIZE")
# The golden has four entries. Three checks compared a module's count to that four, so a module
# with twelve entries - the same design, a bigger module - answered no. That reported 328 modules
# for `no flat decls` and 334 for `functions placed`, and every one of them routed to a conversion
# tool with nothing to convert. What is wrong is a declaration NO table binds, whatever the count.
SIX_H = """#ifndef P_H
#define P_H
#include "protocore_config.h"
#if PROTOCORE_ENABLE_P
PROTOCORE_BEGIN_DECLS
typedef struct { %s } PNs;
%s
static const PNs P __attribute__((unused)) = {
%s
};
PROTOCORE_END_DECLS
#endif
#endif
"""
SIX_C = """#include "protocore_config.h"
#if PROTOCORE_ENABLE_P
#include "p/p.h"
PROTOCORE_BEGIN_DECLS
%s
PROTOCORE_END_DECLS
#endif
"""
NAMES = ["a", "b", "c", "d", "e", "f"]


def sized(names, extra_decl="", extra_def=""):
    """A golden-shaped module with `names` entries, plus anything else to declare and define."""
    h = SIX_H % (
        " ".join("void (*const %s)(uint8_t *restrict work);" % n for n in names),
        "\n".join("void protocore_p_%s(uint8_t *restrict work);" % n for n in names) + "\n" + extra_decl,
        "\n".join("    .%s = protocore_p_%s," % (n, n) for n in names),
    )
    c = SIX_C % (
        "\n".join("void protocore_p_%s(uint8_t *restrict work) { (void)work; }" % n for n in names) + "\n" + extra_def
    )
    th = S.traits(design(h, ".h"), "header")
    tc = S.traits(design(c, ".c"), "source")
    S._cross(th, tc)
    return th, tc


def ask(name, th, tc, ident=None):
    return dict((n, a) for n, a, _ in S.answers(th, tc, gh, gc, ident or gid)).get(name)


th, tc = sized(NAMES)
check("six entries is the same design as four, not a divergence", ask("no flat decls", th, tc) is True)
check("and the six definitions behind them are placed", ask("functions placed", th, tc) is True)
th, tc = sized(NAMES[:2])
check("two entries is the same design too", ask("no flat decls", th, tc) is True)

# What the check is actually for.
th, tc = sized(NAMES, "void protocore_p_reset(uint8_t *restrict work);", "void protocore_p_reset(uint8_t *restrict work) { (void)work; }")
check("a declaration no table binds IS flat", ask("no flat decls", th, tc) is False)
check("and its definition is unplaced", ask("functions placed", th, tc) is False)
print()

print("an accessor for the module's own borrow is not a flat declaration")
# `uint8_t *protocore_<x>_span(void)` cannot be a table entry - an entry takes the borrow and
# returns void, this returns the borrow and takes nothing - and 114 modules publish one so a caller
# with no borrow of its own can still pass a real one. Reading them as flat sent 95 modules to
# `convert gen`, which had nothing it could do with them.
th, tc = sized(NAMES, "uint8_t *protocore_p_span(void);", "uint8_t *protocore_p_span(void) { return 0; }")
check("the accessor is recognised", th["borrow_accessors"] == ["protocore_p_span"])
check("so the header is not flat", ask("no flat decls", th, tc) is True)
check("and the definition is placed", ask("functions placed", th, tc) is True)
# By SIGNATURE, not by name: mmgr's `protocore_span protocore_secure_span(size_t, size_t)` is an
# arena allocator and shares nothing with a borrow accessor but the word.
th, tc = sized(NAMES, "protocore_span protocore_p_span(size_t n, size_t align);", "protocore_span protocore_p_span(size_t n, size_t align) { (void)n; (void)align; }")
check("a function merely NAMED _span is not one", th["borrow_accessors"] == [])
check("so it is still flat", ask("no flat decls", th, tc) is False)
print()

print("the include placement check reads paths, and reads them from the raw text")
# The code mask blanks string literals along with comments, so every include recorded its path as
# "" and the two clauses reading them compared one list of empty strings to another. Both answered
# yes for every module, which is why nothing looked wrong: what they verified was a count.
check("the golden's config include has a path", gh["includes_above_gate"] == ["protocore_config.h"])
check("and the trailing comment is not part of it", all("//" not in p for p in gc["includes_below_gate"]))


def placed(above, below):
    txt = SIX_H % (
        "void (*const a)(uint8_t *restrict work);",
        "void protocore_p_a(uint8_t *restrict work);",
        "    .a = protocore_p_a,",
    )
    txt = txt.replace('#include "protocore_config.h"\n', "".join('#include "%s"\n' % p for p in above))
    txt = txt.replace("PROTOCORE_BEGIN_DECLS", "".join('#include "%s"\n' % p for p in below) + "PROTOCORE_BEGIN_DECLS")
    th_ = S.traits(design(txt, ".h"), "header")
    S._cross(th_, gc)
    return ask("includes placed", th_, gc)


# The gate exists so a disabled module costs nothing. What sits BELOW it is the module's dependency
# list and its length is the module's own business - the golden's header needs no other header,
# fe25519's needs ct_eq.h, and that is the same placement. A clause comparing the two below-gate
# lists for emptiness contradicted the question the check asks, and reported 11 modules for it.
check("a dependency below the gate is placed correctly", placed(["protocore_config.h"], ["crypto/ct_eq.h"]) is True)
check("a header hoisted above the gate is not", placed(["shared/ip/ip.h", "protocore_config.h"], []) is False)
check("nor is a different header standing in for the config", placed(["mmgr/span/span.h"], []) is False)
# With no gate, `above` is the whole file and the question has no subject. `guarded` is the check
# that reports a missing gate, and reporting it here as well makes one defect look like two.
ng = dict(gh, gate_present=False)
ngc = dict(gc, gate_present=False)
check("no gate at all is n/a, not no", ask("includes placed", ng, ngc) is None)
check("and `guarded` still says no", ask("guarded", ng, ngc) is False)
print()

print("FAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
