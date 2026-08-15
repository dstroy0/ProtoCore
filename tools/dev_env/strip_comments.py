#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Strip C comments from a file or tree, leaving the code and the licence header.

An API conversion is driven by patterns over source lines, and prose is what those patterns trip
over: a line-anchored rewrite cannot tell a call from a sentence naming the same call, and a match
that crosses a comment boundary splices the sentence into the code. Removing the comments first
makes the rewrite mechanical.

What is preserved:
  - the leading copyright / SPDX block, which states a licence rather than describing code
  - string and character literals, including escapes, so "http://x" is not read as a comment
  - the line count of block comments, so a compiler error still points at the right line

Usage:
    python tools/dev_env/strip_comments.py PATH [PATH ...]      # dry run: report only
    python tools/dev_env/strip_comments.py PATH --go            # rewrite in place

    --ext .c,.h     which suffixes to visit (default .c,.h)
    --keep-header   keep the leading copyright / SPDX block (default on)
    --no-header     strip that block too
    --exclude PAT   skip any path containing PAT (repeatable)

A file is only rewritten when the result differs, so a second run is a no-op.
"""

import argparse
import io
import os
import sys


def strip(text):
    """Remove // and /* */ comments. Literals survive; block comments leave their newlines."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            q = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == '\\':          # an escape can hide the closing quote
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            i += 2
            while i + 1 < n and not (text[i] == '*' and text[i + 1] == '/'):
                if text[i] == '\n':
                    out.append('\n')
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def header_of(text):
    """The leading copyright / SPDX lines, if the file opens with them."""
    head = []
    for line in text.split('\n'):
        s = line.strip()
        if s.startswith('//') and ('Copyright' in s or 'SPDX' in s or s == '//'):
            head.append(line)
            continue
        break
    return head


def rewrite(text, keep_header):
    head = header_of(text) if keep_header else []
    body = '\n'.join(text.split('\n')[len(head):]) if head else text
    body = strip(body)
    out, blank = [], 0
    for line in body.split('\n'):
        if line.strip() == '':
            blank += 1
            if blank > 1:                    # collapse the runs the removal leaves
                continue
        else:
            blank = 0
        out.append(line.rstrip())
    new = ('\n'.join(head) + '\n' if head else '') + '\n'.join(out).lstrip('\n')
    return new if new.endswith('\n') else new + '\n'


def walk(paths, exts, excludes):
    for p in paths:
        if os.path.isfile(p):
            yield p
            continue
        for dp, _, fns in os.walk(p):
            if any(x in dp.replace('\\', '/') for x in excludes):
                continue
            for f in sorted(fns):
                if os.path.splitext(f)[1] in exts:
                    q = os.path.join(dp, f).replace('\\', '/')
                    if not any(x in q for x in excludes):
                        yield q


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='+')
    ap.add_argument('--go', action='store_true', help='rewrite in place')
    ap.add_argument('--ext', default='.c,.h')
    ap.add_argument('--no-header', dest='keep_header', action='store_false')
    ap.add_argument('--exclude', action='append', default=['__pycache__', '.pio', 'unity_runner.c'])
    a = ap.parse_args()

    exts = set(a.ext.split(','))
    files = changed = removed = 0
    for p in walk(a.paths, exts, a.exclude):
        files += 1
        t = io.open(p, encoding='utf-8', errors='replace', newline='').read()
        new = rewrite(t, a.keep_header)
        if new == t:
            continue
        changed += 1
        removed += t.count('\n') - new.count('\n')
        if a.go:
            io.open(p, 'w', encoding='utf-8', newline='').write(new)

    print('visited %d, would change %d, lines removed %d' % (files, changed, removed)
          if not a.go else
          'visited %d, changed %d, lines removed %d' % (files, changed, removed))
    if not a.go:
        print('DRY RUN - pass --go to rewrite')
    return 0


if __name__ == '__main__':
    sys.exit(main())
