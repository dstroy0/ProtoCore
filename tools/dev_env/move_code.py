#!/usr/bin/env python3
"""Move one or more line ranges from one source file into another.

Cuts the named ranges out of --src and splices them into --dst, either before or
after a regex anchor or appended at the end. Line endings and encoding are
preserved (LF, UTF-8 without BOM), so a move produces no spurious whitespace diff.

Ranges are 1-indexed and inclusive, and are read from the ORIGINAL numbering, so
several --range flags can be given at once without the earlier cuts shifting the
later ones. The moved text is concatenated in the order the flags appear.

Guards are regexes checked against the first and last line of each range before
anything is written; a failed guard aborts with a non-zero exit and no change.

Examples
--------
Move one span before a definition:

    move_code.py --src a.c --dst b.c --range 1523-1674 \\
        --anchor-before '^const SshNetworkNs SshNetwork = ' \\
        --expect-start '^// Frame one built' --expect-end '^\\}$'

Move several spans and append them:

    move_code.py --src a.c --dst b.c --range 100-160 --range 300-355 --append

Preview without writing:

    move_code.py --src a.c --dst b.c --range 10-20 --append --dry-run
"""

from __future__ import annotations

import argparse
import re
import sys


def read_lines(path: str) -> list[str]:
    with open(path, "r", encoding="utf-8", newline="") as fh:
        text = fh.read()
    if "\r\n" in text:
        raise SystemExit(f"{path}: CRLF line endings; this tool only handles LF")
    return text.split("\n")


def write_lines(path: str, lines: list[str]) -> None:
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write("\n".join(lines))


def parse_range(spec: str) -> tuple[int, int]:
    m = re.fullmatch(r"(\d+)-(\d+)", spec.strip())
    if not m:
        raise SystemExit(f"bad --range {spec!r}; want START-END, 1-indexed inclusive")
    start, end = int(m.group(1)), int(m.group(2))
    if start < 1 or end < start:
        raise SystemExit(f"bad --range {spec!r}; need 1 <= START <= END")
    return start, end


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Move line ranges from one file to another.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--src", required=True, help="file to cut from")
    ap.add_argument("--dst", required=True, help="file to splice into")
    ap.add_argument(
        "--range",
        dest="ranges",
        action="append",
        required=True,
        metavar="START-END",
        help="1-indexed inclusive range in the ORIGINAL src numbering; repeatable",
    )
    where = ap.add_mutually_exclusive_group(required=True)
    where.add_argument("--anchor-before", metavar="REGEX", help="insert above the first line matching this")
    where.add_argument("--anchor-after", metavar="REGEX", help="insert below the first line matching this")
    where.add_argument("--append", action="store_true", help="insert at end of dst")
    ap.add_argument("--expect-start", metavar="REGEX", help="guard: first line of every range must match")
    ap.add_argument("--expect-end", metavar="REGEX", help="guard: last line of every range must match")
    ap.add_argument(
        "--back-over-comments",
        action="store_true",
        help="with --anchor-before, move the insertion point up past the comment block above the anchor",
    )
    ap.add_argument("--dry-run", action="store_true", help="report what would move; write nothing")
    args = ap.parse_args()

    src = read_lines(args.src)
    dst = read_lines(args.dst) if args.src != args.dst else src

    spans = [parse_range(r) for r in args.ranges]
    for start, end in spans:
        if end > len(src):
            raise SystemExit(f"--range {start}-{end} runs past {args.src} ({len(src)} lines)")
        if args.expect_start and not re.search(args.expect_start, src[start - 1]):
            raise SystemExit(f"--expect-start failed at {args.src}:{start}\n  {src[start - 1]!r}")
        if args.expect_end and not re.search(args.expect_end, src[end - 1]):
            raise SystemExit(f"--expect-end failed at {args.src}:{end}\n  {src[end - 1]!r}")

    ordered = sorted(spans)
    for (a1, a2), (b1, _) in zip(ordered, ordered[1:]):
        if b1 <= a2:
            raise SystemExit(f"ranges {a1}-{a2} and {b1}- overlap")

    block: list[str] = []
    for start, end in spans:
        block.extend(src[start - 1 : end])

    keep = [ln for i, ln in enumerate(src, start=1) if not any(s <= i <= e for s, e in spans)]

    if args.append:
        at = len(dst)
    else:
        pattern = args.anchor_before or args.anchor_after
        hit = next((i for i, ln in enumerate(dst) if re.search(pattern, ln)), None)
        if hit is None:
            raise SystemExit(f"anchor {pattern!r} not found in {args.dst}")
        at = hit if args.anchor_before else hit + 1
        if args.anchor_before and args.back_over_comments:
            while at > 0 and (dst[at - 1].lstrip().startswith("//") or not dst[at - 1].strip()):
                at -= 1

    out = dst[:at] + block + [""] + dst[at:]

    print(f"moving {len(block)} lines in {len(spans)} span(s)")
    for start, end in spans:
        print(f"  {args.src}:{start}-{end}  {src[start - 1].strip()[:64]}")
    print(f"  -> {args.dst} at line {at + 1}")
    print(f"{args.src}: {len(src)} -> {len(keep)} lines")
    print(f"{args.dst}: {len(dst)} -> {len(out)} lines")

    if args.dry_run:
        print("dry run; nothing written")
        return 0
    if args.src == args.dst:
        raise SystemExit("--src and --dst must differ")

    write_lines(args.dst, out)
    write_lines(args.src, keep)
    return 0


if __name__ == "__main__":
    sys.exit(main())
