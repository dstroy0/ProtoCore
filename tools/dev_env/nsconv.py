"""Shared call-rewriting primitives, correct about the three things my ad-hoc converters got wrong:

1. String and character literals. A '(' or ',' inside "a\"b" or ')' is not syntax, so every scan
   here steps over literals rather than counting their characters.
2. Where staging goes. A converted call becomes several assignments plus an entry call. Those are
   statements, so they belong above the whole enclosing STATEMENT - never at the start of the line
   the call happens to sit on, which inside a multi-line macro argument list is between the macro's
   arguments and does not compile.
3. Loop conditions. `while ((n = f(x)) > 0)` re-evaluates the call every iteration; hoisting it
   above the loop calls it once and spins forever. The tool cannot rewrite that faithfully, so it
   refuses and reports instead of guessing.
"""

import re

from codemask import code_mask


def _skip_literal(s, i):
    """i indexes the opening quote; returns the index just past the closing one."""
    q = s[i]
    i += 1
    while i < len(s):
        if s[i] == "\\":
            i += 2
            continue
        if s[i] == q:
            return i + 1
        i += 1
    return i


def close_paren(s, open_after):
    """open_after indexes just past a '('; returns the index just past its matching ')'."""
    i, depth = open_after, 1
    while i < len(s) and depth:
        c = s[i]
        if c in "\"'":
            i = _skip_literal(s, i)
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        i += 1
    return i


def split_args(txt):
    """Top-level comma split, literal-aware."""
    out, depth, cur, i = [], 0, [], 0
    while i < len(txt):
        c = txt[i]
        if c in "\"'":
            j = _skip_literal(txt, i)
            cur.append(txt[i:j])
            i = j
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        if c == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


def has_statement_separator(txt):
    """True when txt holds a ';' that is real syntax rather than text inside a literal.

    An HTML entity in a string ("a&lt;b") carries a ';' that is not a statement separator, which is
    why this steps over literals instead of searching the raw text.
    """
    i = 0
    while i < len(txt):
        c = txt[i]
        if c in "\"'":
            i = _skip_literal(txt, i)
            continue
        if c == ";":
            return True
        i += 1
    return False


def _prev_significant(s, i):
    """Walk back from i over whitespace and literals; returns the index of the previous char."""
    while i > 0 and s[i - 1] in " \t\r\n":
        i -= 1
    return i


def _brace_is_compound_literal(s, brace, mask):
    """True when s[brace] == '{' opens a compound literal rather than a block.

    `(T[]){...}`, `if (x) {` and `void f(void) {` all put a ')' before the brace, so the ')' decides
    nothing. What separates them is the character immediately before the '(' that ')' closes: an
    identifier character means a call, a definition or a control statement, so the brace opens a
    block; anything else (a comma, an '=', an open paren, the start of a statement) means the parens
    were a cast, so the brace opens an initialiser.
    """
    j = brace - 1
    while j > 0 and (not mask[j] or s[j] in " \t\r\n"):
        j -= 1
    if j <= 0 or s[j] != ")":
        return False
    depth = 0
    while j > 0:
        if mask[j]:
            if s[j] == ")":
                depth += 1
            elif s[j] == "(":
                depth -= 1
                if depth == 0:
                    break
        j -= 1
    k = j - 1
    while k > 0 and (not mask[k] or s[k] in " \t\r\n"):
        k -= 1
    return not (k >= 0 and (s[k].isalnum() or s[k] == "_"))


def statement_start(s, pos, mask=None):
    """Index of the first non-space character of the statement containing pos.

    Walks back to the nearest ';', '{', '}' or ':' at nesting depth zero, stepping over any '(' or
    '[' that opened before pos (a call or macro argument list the position sits inside - the
    statement began before it). Comment and literal bytes are text, not syntax: an apostrophe in
    "Setter's" is not a char literal and parens in prose do not nest, so both are skipped.
    """
    if mask is None:
        mask = code_mask(s)
    i, depth = pos, 0
    while i > 0:
        if not mask[i - 1]:
            i -= 1
            continue
        c = s[i - 1]
        if c in ")]":
            depth += 1
        elif c in "([":
            if depth == 0:
                i -= 1  # an argument list we are inside: the statement started before it
                continue
            depth -= 1
        elif c == "}":
            if depth == 0:
                break  # a complete block precedes us: it ended the previous statement
            depth += 1
        elif c == "{":
            # `(T[]){ ... }` is a compound literal, not a block: stopping at its brace put the
            # staging inside the initialiser. `if (x) {` looks the same until you read past the '('.
            if depth:
                depth -= 1
            elif _brace_is_compound_literal(s, i - 1, mask):
                i -= 1
                continue
            else:
                break
        elif depth == 0 and c in ";:":
            break
        i -= 1
    # Forward to the statement's own first character. A comment between the separator and the
    # statement is text, so it is stepped over: stopping on it puts the statement start on the
    # PREVIOUS line, and the staging hoisted there lands above the statement before this one.
    while i < len(s):
        if s[i] in " \t\r\n":
            i += 1
        elif s[i : i + 2] == "//":
            i = s.find("\n", i)
            if i == -1:
                return len(s)
        elif s[i : i + 2] == "/*":
            j = s.find("*/", i + 2)
            i = len(s) if j == -1 else j + 2
        else:
            break
    return i


LOOP = re.compile(r"\b(while|for)\s*\($")


def after_short_circuit(s, pos, mask=None):
    """True when the call at pos sits to the right of an `&&` or `||` in the same statement.

    Hoisting it above the statement runs it before the operand that gates it, and unconditionally.
    `if (!r8(REG, &irq) || !data_ready(irq))` is the shape: r8 fills irq and the right operand reads
    it, so a hoisted data_ready sees the value from before the read - and runs even when r8 already
    decided the answer.
    """
    if mask is None:
        mask = code_mask(s)
    depth = 0
    i = pos - 1
    while i >= 0:
        if not mask[i]:
            i -= 1
            continue
        c = s[i]
        if c in ")]}":
            depth += 1
        elif c in "([{":
            if depth == 0:
                break  # out to the enclosing call or condition: no operator gated us
            depth -= 1
        elif depth == 0 and c in "&|" and i > 0 and s[i - 1] == c:
            return True
        elif depth == 0 and c == ";":
            break
        i -= 1
    return False

# Macros that evaluate an argument more than once. Hoisting a call out of one of these is the
# loop-condition mistake wearing a macro: DBENCH_OP(label, n, expr) runs expr n times to time it,
# so a call lifted above it is measured once and the benchmark then times an addition. DBENCH_BULK
# hands its expr to the same DBENCH_CYCLES loop and was missed, which is how one bench came out
# timing `sink += Ns.n`.
REPEATING = ("DBENCH_OP", "DBENCH_BULK", "DBENCH_CYCLES")


def in_repeating_macro(s, pos, mask=None):
    """True when the call at pos is inside an argument of a macro that re-evaluates it."""
    if mask is None:
        mask = code_mask(s)
    i, depth = pos, 0
    while i > 0:
        if not mask[i - 1]:
            i -= 1
            continue
        c = s[i - 1]
        if c in ")]":
            depth += 1
        elif c in "([":
            if depth == 0:
                head = re.search(r"(\w+)\s*$", s[: i - 1])
                if c == "(" and head and head.group(1) in REPEATING:
                    return True
            else:
                depth -= 1
        elif depth == 0 and c in ";{}":
            break
        i -= 1
    return False


def in_loop_condition(s, pos, mask=None):
    """True when the call at pos sits inside a while/for controlling expression."""
    st = statement_start(s, pos, mask)
    head = s[st:pos]
    if re.match(r"\s*(while|for)\s*\(", head):
        return True
    # `do { ... } while (f(x));` - the statement starts at the 'while'
    return bool(re.match(r"\s*while\s*\(", head))


def line_start(s, pos):
    return s.rfind("\n", 0, pos) + 1


def indent_of(s, pos):
    return re.match(r"[ \t]*", s[line_start(s, pos) :]).group(0)


def calls_in_statement(s, pos, pattern, mask=None):
    """How many calls matching `pattern` share the statement containing pos."""
    if mask is None:
        mask = code_mask(s)
    st = statement_start(s, pos, mask)
    end = pos
    depth = 0
    while end < len(s):
        if not mask[end]:
            end += 1
            continue
        c = s[end]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif c == ";" and depth == 0:
            break
        end += 1
    # count only matches that are code: a bench label like "protocore_config_get_str (trunc)"
    # names the entry inside a string literal, and that is text, not a second call.
    return sum(1 for m in pattern.finditer(s, st, end + 1) if mask[m.start()])


def rewrite(s, call_start, call_end, staging, value, pattern=None, mask=None):
    """Replace s[call_start:call_end] with `value`, hoisting `staging` above the statement.

    When the call was the whole statement (its value unused) the statement and its ';' go away and
    only the staging remains. Raises when the call is a loop condition: see the module docstring.
    """
    if mask is None:
        mask = code_mask(s)
    if in_loop_condition(s, call_start, mask):
        raise ValueError("call is a loop condition; rewrite it by hand")
    if in_repeating_macro(s, call_start, mask):
        raise ValueError("call is inside a macro that re-evaluates it; rewrite it by hand as (Entry(w), read)")
    if after_short_circuit(s, call_start, mask):
        raise ValueError(
            "call is the right operand of && or ||; hoisting runs it before the operand "
            "that gates it, and unconditionally. Rewrite it by hand"
        )
    # Two calls to the same namespace in one statement cannot both be hoisted: the namespace has one
    # result member, so the first value is overwritten by the second and both reads see the last.
    # memcmp(f(0)->h, f(1)->h, n) would silently compare a value with itself.
    if pattern is not None and calls_in_statement(s, call_start, pattern, mask) > 1:
        raise ValueError(
            "two calls to this namespace share one statement; capture the first result "
            "into a local and rewrite by hand"
        )
    st = statement_start(s, call_start, mask)
    ls = line_start(s, st)
    indent = re.match(r"[ \t]*", s[ls:]).group(0)
    body = "\n".join(indent + line for line in staging)
    lead = s[ls:call_start]  # the statement's own text before the call, indentation included
    if lead.strip() == "":
        j = call_end
        while j < len(s) and s[j] in " \t":
            j += 1
        if s[j : j + 1] == ";":
            return s[:ls] + body + s[j + 1 :]
    return s[:ls] + body + "\n" + lead + value + s[call_end:]
