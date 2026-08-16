"""Which byte offsets of a C file are code, and which are comment or literal.

A converter that rewrites call sites must not rewrite a call that appears inside a /** doc block */
or a // line comment: the text there describes the API, it does not invoke it. Rewriting it injects
statements into the middle of a comment and silently corrupts the file.
"""


def code_mask(s):
    """A bytearray parallel to s: 1 where the byte is code, 0 where it is comment or literal text."""
    m = bytearray(b"\x01" * len(s))
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == "/" and i + 1 < n and s[i + 1] == "/":
            while i < n and s[i] != "\n":
                m[i] = 0
                i += 1
            continue
        if c == "/" and i + 1 < n and s[i + 1] == "*":
            m[i] = m[i + 1] = 0
            i += 2
            while i < n and not (s[i] == "*" and i + 1 < n and s[i + 1] == "/"):
                m[i] = 0
                i += 1
            if i < n:
                m[i] = 0
                if i + 1 < n:
                    m[i + 1] = 0
                i += 2
            continue
        if c in "\"'":
            q = c
            m[i] = 0
            i += 1
            while i < n:
                if s[i] == "\\":
                    m[i] = 0
                    if i + 1 < n:
                        m[i + 1] = 0
                    i += 2
                    continue
                m[i] = 0
                if s[i] == q:
                    i += 1
                    break
                i += 1
            continue
        i += 1
    return m


def is_code(mask, pos):
    return pos < len(mask) and mask[pos] == 1
