import os, sys

sys.path.insert(0, r"C:\Users\Douglas\.claude\jobs\2bafe6f6\tmp")
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

print()
print("FAILURES:", fails)
sys.exit(1 if fails else 0)
