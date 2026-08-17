import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8")
import nsconv as N

fails = 0


def eq(name, got, want):
    global fails
    if got != want:
        fails += 1
        print("FAIL %s\n  got:  %r\n  want: %r" % (name, got, want))
    else:
        print("ok   %s" % name)


# 1. a ')' inside a string literal must not close the call - the bug that truncated gcm(key)
s = 'f(a, ")", b)'
eq("literal paren", s[2 : N.close_paren(s, 2) - 1], 'a, ")", b')

# 2. a ',' inside a literal is not an argument separator
eq("literal comma", N.split_args('a, "x,y", b'), ["a", '"x,y"', "b"])

# 3. a nested call keeps its own parens
eq("nested call", N.split_args("gcm(key), n12, NULL"), ["gcm(key)", "n12", "NULL"])

# 4. an escaped quote does not end the literal
eq("escaped quote", N.split_args('"a\\"b,c", d'), ['"a\\"b,c"', "d"])

# 5. the statement start of a call on a macro CONTINUATION line is the macro, not the line
src = (
    "void t(void)\n"
    "{\n"
    "    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_3,\n"
    "                            old_call(a, b));\n"
    "}\n"
)
pos = src.index("old_call")
eq(
    "stmt start crosses the macro line",
    src[N.statement_start(src, pos) :].split("\n")[0].strip(),
    "TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_3,",
)

# 6. rewriting that call hoists staging ABOVE the assert and leaves the value inside it
end = N.close_paren(src, pos + len("old_call("))
out = N.rewrite(src, pos, end, ["Ns.args.a = a;", "Ns.args.b = b;", "Ns.entry(w);"], "Ns.value")
eq(
    "hoist above the assert",
    out,
    "void t(void)\n"
    "{\n"
    "    Ns.args.a = a;\n"
    "    Ns.args.b = b;\n"
    "    Ns.entry(w);\n"
    "    TEST_ASSERT_EQUAL_HEX16(TLS_VERSION_1_3,\n"
    "                            Ns.value);\n"
    "}\n",
)

# 7. a call that IS the statement loses its ';' and leaves only staging
src2 = "void t(void)\n{\n    old_call(a, b);\n}\n"
pos2 = src2.index("old_call")
end2 = N.close_paren(src2, pos2 + len("old_call("))
eq(
    "bare statement",
    N.rewrite(src2, pos2, end2, ["Ns.args.a = a;", "Ns.entry(w);"], "Ns.value"),
    "void t(void)\n{\n    Ns.args.a = a;\n    Ns.entry(w);\n}\n",
)

# 8. a call used as a loop condition keeps the loop's shape (staging hoisted above the whole 'while')
src3 = "void t(void)\n{\n    while ((n = old_call(a)) > 0)\n    {\n    }\n}\n"
pos3 = src3.index("old_call")
end3 = N.close_paren(src3, pos3 + len("old_call("))
try:
    N.rewrite(src3, pos3, end3, ["Ns.entry(w);"], "Ns.n")
    eq("loop condition refused", "converted", "refused")
except ValueError:
    eq("loop condition refused", "refused", "refused")

# 9. a trailing comment on the PREVIOUS statement is text: the statement start is the call's own
# line, so the staging does not land above the statement before it
src4 = "void t(void)\n{\n    fill(1); // the oldest slot\n\n    old_call(a);\n}\n"
pos4 = src4.index("old_call")
end4 = N.close_paren(src4, pos4 + len("old_call("))
eq(
    "trailing comment does not move the statement start",
    N.rewrite(src4, pos4, end4, ["Ns.args.a = a;", "Ns.entry(w);"], "Ns.ok"),
    "void t(void)\n{\n    fill(1); // the oldest slot\n\n    Ns.args.a = a;\n    Ns.entry(w);\n}\n",
)

# 10. and neither does a block comment sitting between the two statements
src5 = "void t(void)\n{\n    fill(1);\n    /* the oldest slot */\n    old_call(a);\n}\n"
pos5 = src5.index("old_call")
end5 = N.close_paren(src5, pos5 + len("old_call("))
eq(
    "block comment does not move the statement start",
    N.rewrite(src5, pos5, end5, ["Ns.entry(w);"], "Ns.ok"),
    "void t(void)\n{\n    fill(1);\n    /* the oldest slot */\n    Ns.entry(w);\n}\n",
)

# 11. a call inside a macro that re-evaluates its argument is refused: hoisting it above DBENCH_OP
# would time an addition and call the entry once
src6 = 'void t(void)\n{\n    DBENCH_OP("x", 200000, sink += old_call(a));\n}\n'
pos6 = src6.index("old_call")
end6 = N.close_paren(src6, pos6 + len("old_call("))
try:
    N.rewrite(src6, pos6, end6, ["Ns.entry(w);"], "Ns.n")
    eq("repeating macro refused", "converted", "refused")
except ValueError:
    eq("repeating macro refused", "refused", "refused")

# 11a. the right operand of || is refused: `if (!r8(REG, &irq) || !old_call(irq))` fills irq in the
# left operand and reads it in the right, so a hoisted call sees the value from before the read
src6a = "void t(void)\n{\n    if (!r8(REG, &irq) || !old_call(irq))\n    {\n        return;\n    }\n}\n"
pos6a = src6a.index("old_call")
end6a = N.close_paren(src6a, pos6a + len("old_call("))
try:
    N.rewrite(src6a, pos6a, end6a, ["Ns.entry(w);"], "Ns.ok")
    eq("short-circuit right operand refused", "converted", "refused")
except ValueError:
    eq("short-circuit right operand refused", "refused", "refused")

# 11a2. and && the same way
src6c = "void t(void)\n{\n    if (ready() && old_call(a))\n    {\n        return;\n    }\n}\n"
pos6c = src6c.index("old_call")
end6c = N.close_paren(src6c, pos6c + len("old_call("))
try:
    N.rewrite(src6c, pos6c, end6c, ["Ns.entry(w);"], "Ns.ok")
    eq("short-circuit && right operand refused", "converted", "refused")
except ValueError:
    eq("short-circuit && right operand refused", "refused", "refused")

# 11a3. but the LEFT operand has nothing before it to be gated by, so it still converts
src6d = "void t(void)\n{\n    if (old_call(a) || fallback())\n    {\n        return;\n    }\n}\n"
pos6d = src6d.index("old_call")
end6d = N.close_paren(src6d, pos6d + len("old_call("))
eq(
    "short-circuit left operand still converts",
    N.rewrite(src6d, pos6d, end6d, ["Ns.entry(w);"], "Ns.ok"),
    "void t(void)\n{\n    Ns.entry(w);\n    if (Ns.ok || fallback())\n    {\n        return;\n    }\n}\n",
)

# 11b. DBENCH_BULK hands its expr to the same DBENCH_CYCLES loop, and was missed once: a bench came
# out with the entry hoisted above it, timing `sink += Ns.n`
src6b = 'void t(void)\n{\n    DBENCH_BULK("x", 50000, 21, sink += old_call(a));\n}\n'
pos6b = src6b.index("old_call")
end6b = N.close_paren(src6b, pos6b + len("old_call("))
try:
    N.rewrite(src6b, pos6b, end6b, ["Ns.entry(w);"], "Ns.n")
    eq("DBENCH_BULK refused", "converted", "refused")
except ValueError:
    eq("DBENCH_BULK refused", "refused", "refused")

# 12. and a TEST_ASSERT, which evaluates its argument once, is still converted
src7 = 'void t(void)\n{\n    TEST_ASSERT_EQUAL_INT(3, old_call(a));\n}\n'
pos7 = src7.index("old_call")
end7 = N.close_paren(src7, pos7 + len("old_call("))
eq(
    "single-evaluation macro still converts",
    N.rewrite(src7, pos7, end7, ["Ns.entry(w);"], "Ns.n"),
    "void t(void)\n{\n    Ns.entry(w);\n    TEST_ASSERT_EQUAL_INT(3, Ns.n);\n}\n",
)

# 13. a `#if` / `#endif` bounds the arm. The walk back used to run past both and land the staging in
# the PREVIOUS arm, so the call ran under another capability's gate and the read ran under none.
src8 = (
    "void t(void)\n"
    "{\n"
    "#if A\n"
    "    reg(one());\n"
    "#endif\n"
    "#if B\n"
    "    reg(old_call());\n"
    "#endif\n"
    "}\n"
)
pos8 = src8.index("old_call")
end8 = N.close_paren(src8, pos8 + len("old_call("))
eq(
    "staging stays inside its own #if arm",
    N.rewrite(src8, pos8, end8, ["Ns.entry(w);"], "Ns.ptr"),
    "void t(void)\n{\n#if A\n    reg(one());\n#endif\n#if B\n    Ns.entry(w);\n    reg(Ns.ptr);\n#endif\n}\n",
)

# 13b. the first statement of an arm has the directive directly above it and nothing else
src8b = "void t(void)\n{\n#if B\n    reg(old_call());\n#endif\n}\n"
pos8b = src8b.index("old_call")
end8b = N.close_paren(src8b, pos8b + len("old_call("))
eq(
    "first statement in an arm hoists below the #if",
    N.rewrite(src8b, pos8b, end8b, ["Ns.entry(w);"], "Ns.ptr"),
    "void t(void)\n{\n#if B\n    Ns.entry(w);\n    reg(Ns.ptr);\n#endif\n}\n",
)

# 13c. a plain statement above the call is still where the staging goes: a directive is a boundary,
# not a magnet
src8c = "void t(void)\n{\n    int a = 1;\n    reg(old_call());\n}\n"
pos8c = src8c.index("old_call")
end8c = N.close_paren(src8c, pos8c + len("old_call("))
eq(
    "no directive, no change",
    N.rewrite(src8c, pos8c, end8c, ["Ns.entry(w);"], "Ns.ptr"),
    "void t(void)\n{\n    int a = 1;\n    Ns.entry(w);\n    reg(Ns.ptr);\n}\n",
)

print()
print("FAILURES:", fails)
sys.exit(1 if fails else 0)
