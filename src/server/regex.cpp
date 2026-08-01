// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file regex.cpp
 * @brief Bounded regex route matcher for PC (used by on_regex() routes).
 *
 * Split out of protocore.cpp (single-purpose server files). A small recursive backtracker over
 * one pattern - literals, '.', quantifiers, character classes, and escapes - with a step budget
 * (RE_MAX_STEPS) so a pathological pattern fails closed instead of backtracking unboundedly,
 * preserving determinism. No heap, no groups, no alternation. The route dispatcher calls
 * regex_match() (declared in protocore.h). Behavior is identical to the pre-split code.
 */

#include "protocore.h" // RE_MAX_STEPS (ServerConfig), fixed-width types
#include <string.h>

// ---------------------------------------------------------------------------
// Bounded regex route matcher (see on_regex()).
//
// A small recursive backtracker over a single pattern (no heap, no groups, no
// alternation). Supported: literals, '.', quantifiers '*' '+' '?', character
// classes [..]/[^..] with a-z ranges, and '\' escapes incl. \d \w \s (\D \W \S).
// A step counter bounds total work so a pathological pattern fails closed
// (no match) instead of backtracking unboundedly - preserving determinism.
// ---------------------------------------------------------------------------

struct ReCtx
{
    uint32_t steps;
    uint32_t max_steps;
};

// Byte length of the atom at p: an escape (\x), a class ([...]), or one char.
static size_t re_atom_len(const char *p)
{
    if (*p == '\\')
    {
        return p[1] ? 2 : 1;
    }
    if (*p == '[')
    {
        const char *q = p + 1;
        if (*q == '^')
        {
            q++;
        }
        if (*q == ']') // a ']' right after '[' (or '[^') is a literal member
        {
            q++;
        }
        while (*q && *q != ']')
        {
            if (*q == '\\' && q[1])
            {
                q += 2;
            }
            else
            {
                q++;
            }
        }
        return (size_t)((*q == ']' ? q + 1 : q) - p);
    }
    return 1;
}

static bool re_class_member(char lo, char hi, char ch)
{
    return ch >= lo && ch <= hi;
}

// Read one class atom at *q (a backslash-escape consumes 2 bytes, else 1), advancing q past it.
static char re_read_atom(const char **q, const char *end)
{
    const char *p = *q;
    if (*p == '\\' && (p + 1) < end)
    {
        char c = p[1];
        *q = p + 2;
        return c;
    }
    char c = *p;
    *q = p + 1;
    return c;
}

// Match a backslash-escape class (\d \D \w \W \s \S) or an escaped literal against ch.
static bool re_match_escape(char e, char ch)
{
    switch (e)
    {
    case 'd':
        return ch >= '0' && ch <= '9';
    case 'D':
        return !(ch >= '0' && ch <= '9');
    case 'w':
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
    case 'W':
        return !((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_');
    case 's':
        return ch == ' ' || ch == '\t';
    case 'S':
        return !(ch == ' ' || ch == '\t');
    default:
        return ch == e; // escaped literal (\. \* \\ ...)
    }
}

// Match a '[...]' character class (optional '^' negation, a-z ranges) at [p, p+len) against ch.
static bool re_match_class(const char *p, size_t len, char ch)
{
    const char *q = p + 1;
    const char *end = p + len - 1; // points at the closing ']'
    bool neg = false;
    if (q < end && *q == '^')
    {
        neg = true;
        q++;
    }
    bool m = false;
    while (q < end)
    {
        char lo = re_read_atom(&q, end);
        // The q[1] != ']' arm below can never be false, so this line is branch-excluded. re_atom_len
        // ends the class at the FIRST unescaped ']', so if q[1] is a ']' it IS the terminator - then
        // q + 1 == end and the preceding (q + 1) < end has already short-circuited. Confirmed by
        // exhaustive search over every class body of length <= 7 drawn from { [ ] ^ - \ a }.
        if (q < end && *q == '-' && (q + 1) < end && q[1] != ']') // GCOVR_EXCL_LINE  see note above
        {
            q++; // consume '-'
            char hi = re_read_atom(&q, end);
            m = re_class_member(lo, hi, ch) || m;
        }
        else if (ch == lo)
        {
            m = true;
        }
    }
    return neg ? !m : m;
}

// Does the atom [p, p+len) match the single character ch (ch != '\0')?
static bool re_atom_matches(const char *p, size_t len, char ch)
{
    if (ch == '\0')
    {
        return false;
    }
    if (*p == '\\')
    {
        return re_match_escape(p[1], ch);
    }
    if (*p == '.')
    {
        return true;
    }
    if (*p == '[')
    {
        return re_match_class(p, len, ch);
    }
    return ch == *p; // literal
}

static bool re_match(ReCtx *c, const char *pat, const char *text);

// Greedy "(atom)* rest" against text.
static bool re_star(ReCtx *c, const char *atom, size_t al, const char *rest, const char *text)
{
    c->steps++;
    if (c->steps > c->max_steps)
    {
        return false;
    }
    if (re_atom_matches(atom, al, *text) && re_star(c, atom, al, rest, text + 1))
    {
        return true;
    }
    return re_match(c, rest, text);
}

static bool re_match(ReCtx *c, const char *pat, const char *text)
{
    c->steps++;
    if (c->steps > c->max_steps)
    {
        return false;
    }
    if (*pat == '\0')
    {
        return *text == '\0'; // full-match: pattern and text end together
    }

    size_t al = re_atom_len(pat);
    char quant = pat[al];
    const char *rest = (quant == '*' || quant == '+' || quant == '?') ? pat + al + 1 : pat + al;

    if (quant == '*')
    {
        return re_star(c, pat, al, rest, text);
    }
    if (quant == '+')
    {
        if (!re_atom_matches(pat, al, *text))
        {
            return false;
        }
        return re_star(c, pat, al, rest, text + 1);
    }
    if (quant == '?')
    {
        if (re_atom_matches(pat, al, *text) && re_match(c, rest, text + 1))
        {
            return true;
        }
        return re_match(c, rest, text);
    }
    // exactly one
    if (re_atom_matches(pat, al, *text))
    {
        return re_match(c, rest, text + 1);
    }
    return false;
}

// Whole-path regex match (implicitly anchored at both ends). External linkage
// (declared in protocore.h): the route dispatcher calls it.
bool regex_match(const char *pattern, const char *path)
{
    ReCtx c;
    c.steps = 0;
    c.max_steps = RE_MAX_STEPS;
    return re_match(&c, pattern, path);
}
