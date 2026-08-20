#!/usr/bin/env python3
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""harness.py - the one entry point for tools/.

Everything under tools/ is reachable from here, so `harness.py help` is the whole surface and
`harness.py list` is the whole inventory:

  list          every script under tools/, grouped, with its flags and what it writes
  help          every command's help in one call, or one command's

  ci            hand off to tools/ci_tooling/ci.py: gen, check, baseline, cov, fmt, sonar
  convert       the module conversion family: scan, gen, shape, pimpl, funnel, nsmap
  edit          mechanical source edits: move, comments
  view          read-only readers: png, nodeset, tree, conform
  measure       includes, pid, and the three standing probes
  crypto        test vectors and keys
  assets        diagrams, theme previews, favicons, svg tooltips
  hooks         git: commit, install, status, cspell, dependabot
  build         THE BUILD: modules, cmake, split - plus envs, ccache, psram
  selftest      run the tools' own self-tests
  doc gen       regenerate the derived tables in tools/TOOLS.md

The point is discovery. Half these scripts have names that do not say what they do, which is how
the same tool gets written twice - and a tool nobody can find gets written a second time. One entry
point means what a session learns about driving the tooling does not have to be re-derived.

HOW THIS TREE BUILDS - READ THIS BEFORE TOUCHING A BUILD FILE

  CMake, GENERATED FROM THE TREE. Every CMakeLists.txt under src/ says "GENERATED ... Do not edit"
  and means it: a hand edit is reverted by the next `build modules` and reported as drift by
  `ci check module_graph` in between. Change the generator, never the output.

  A MODULE IS A DIRECTORY. One .c, the .h beside it, and the CMakeLists.txt declaring the target.
  A new .c/.h pair dropped beside an existing one is wrong - `build split` moves it into its own
  directory and rewrites every reference.

    build modules    439 CMakeLists.txt under src/, one per directory
    build cmake      test/CMakeLists.txt: one target and one ctest per env
    build split      a pair still sharing a directory (dry run by default)

  Dependencies are read out of what each module includes, split by WHICH FILE included it: a
  header's includes are PUBLIC (they propagate, and only they can close a cycle), a .c's are
  PRIVATE. Two .c files calling each other's published handle is ordinary C, not a cycle.

  CTEST RUNS THE TESTS. Every one of them, every time, at 0.06 s per test:

    tools/harness.py build cmake                                  regenerate test/CMakeLists.txt
    cmake -S test -B build/native
    cmake --build build/native -j                                 incremental; -- -k for every failure
    ctest --test-dir build/native -j                              the whole matrix
    ctest --test-dir build/native -R native_<env> --output-on-failure    one env

  `test/harness.py run <env>` compiles an env from scratch by itself and does not read the CMake.
  A from-scratch single-env compile is slower than building and running the WHOLE matrix here, so
  it is never the faster check and never the right first move. Build with cmake, run with ctest.

A HWCAP IS NEVER REMOVED. NOT ONCE, NOT TO MAKE A LINK SUCCEED.

  A capability arm is a `#if PROTOCORE_HAS_HW_<X>` half and a `#if !PROTOCORE_HAS_HW_<X>` half.
  Both ship. When an env fails to link a symbol a hw arm defines, that is the CAP BEING OFF, and
  the fix is to turn it on and build the arm - never to swap in the portable half, never to delete
  the hw source from the env, never to drop the flag.

  A hw arm that lives in src/ and calls vendor silicon MOVES to test/core_setup/hal/<vendor>/ and
  is guarded there. It does not get deleted: moving it is what keeps the vendor cap testable on
  the host. Both arms always stay.

  THE HOST DRIVES EVERYTHING - BOTH ARMS. The two halves are mutually exclusive, so one env cannot
  hold both: give each its own env, same suite in both.

    env add native_x_sw --after native_x --clone native_x
    env update native_x_sw --drop-src <host arm> --src <portable arm> \
                           --drop-flags=-DPROTOCORE_HAS_HW_X=1 --flags=-DPROTOCORE_HAS_HW_X=0

  Audit before finishing any matrix work. Every hit must be a path rename, not a removal:

    git diff test/test_matrix.json | grep "^-" | grep -iE "hal/|_hw|HW_"

THE TWO THINGS THAT COST THE MOST TIME

  1. `--dry` FIRST, ALWAYS, on every writing subcommand. `convert gen`, `convert pimpl`,
     `convert funnel` and `convert nsmap` all take it, and it prints the diff it WOULD apply while
     writing nothing. The dry run is what catches a wrong result-member name, a mangled include,
     and a benchmark that would silently stop measuring anything. Read it before it lands, not
     after.
  2. NEVER hand-edit a generated region. Every generator under ci_tooling/generate/ takes
     `--check`, which is what CI gates on, so a hand edit inside a region is reverted by the next
     `harness.py ci gen` and reported as drift in between. Change the generator.

WHERE A TOOL GOES

  ci_tooling/check/       fails CI on a violation, writes nothing
  ci_tooling/generate/    writes into a tracked file, always through lib/doc_region.py
  ci_tooling/lib/         imported, never run
  ci_tooling/coverage/    coverage planning, running, report merging
  crypto/                 test DATA: vectors and keys
  dev_env/                the conversion tools and the readers, for a session, not for CI
  git-hooks/              the hooks themselves

  Read tools/ci_tooling/README.md before adding anything to ci_tooling: three generators had
  already solved the prettier collision with `prettier-ignore` fences when a fourth was written,
  did not know, and invented a second mechanism that silently disabled the drift detection it was
  meant to provide.

ADDING A TOOL IS THREE STEPS

  1. write it in the directory above that matches what it does
  2. add one line to the table in this file, with a usage and a hint that says the thing a first
     run gets wrong - the hint is the whole reason this file is worth reading
  3. `harness.py doc gen` - tools/TOOLS.md's tables are derived from the code, so the flags, the
     write primitives and the external commands come out of the file itself, not out of a
     docstring. A docstring is a claim; treat one that disagrees with the table as the thing that
     is wrong. Forgetting this step is caught rather than silent: CI runs the same generator as
     `ci gen --check tools_inventory` on every pull request, and regenerates it on push to main.

THIS IS NOT THE TEST HARNESS. test/harness.py owns the test matrix, the native runs, bare metal
and the benchmarks, and it is the one entry point for all of those. Nothing here writes
test/test_matrix.json, and nothing here should: a whole-file re-render is valid JSON and reformats
all 400 envs, so the diff stops naming what changed.
"""

import argparse
import ast
import os
import re
import runpy
import shutil
import subprocess
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from tools import findroot  # noqa: E402

ROOT = findroot.root()
DOC = findroot.at("tools", "TOOLS.md")


# ---------------------------------------------------------------------------
# the tables: one line per runnable tool, with the hint a first run needs
# ---------------------------------------------------------------------------


class Tool(object):
    """One runnable entry: where it lives, how it is called, and what a first run gets wrong."""

    __slots__ = ("path", "usage", "hint", "argv", "fn")

    def __init__(self, path, usage, hint, argv=()):
        self.path = path
        self.usage = usage
        self.hint = " ".join(hint.split())
        self.argv = tuple(argv)
        self.fn = None


def T(path, usage, hint, argv=()):
    return Tool(path, usage, hint, argv)


GOLDENIZE = "tools/dev_env/goldenize.py"

CONVERT = {
    "scan": T(
        GOLDENIZE,
        "scan <module.h>",
        "READ src/crypto/hash/sha256/sha256.h AND sha256.c FIRST. They are the target, and reading "
        "them answers what otherwise looks like a design question: there is NO caller-visible state "
        "struct (the borrow IS the running state, so two documents are two borrows), EVERY operand "
        "is an args member rather than a parameter, and the .c splits the borrow into named regions "
        "at compile-time offsets under one static_assert with ONE pointer per translation unit. Do "
        "not widen a flat signature, do not add a config struct, and do not pull in a module to "
        "supply an operand - the caller states it. "
        "Prints the spec it infers, as JSON, and writes nothing. Check three fields before "
        'gen: "pool" (the scanner always writes "secure" because it cannot read what a module '
        "holds - key material takes the secure end, everything else the plaintext one), each "
        'entry\'s "result" member (two entries whose returns map to the same width share one '
        'declaration and the wider one is truncated), and "entry" (the flat prefix does not '
        "always match the module name). A module that is ALREADY golden has no flat declarations "
        "to read, so scanning it returns an empty spec - rebuild from the converted header "
        "instead.",
        ["scan"],
    ),
    "gen": T(
        GOLDENIZE,
        "gen <spec.json> [--dry]",
        "Writes the header, restructures the .c, rewrites the call sites, runs the shape pass and "
        "clang-format, and generates the suite's Unity runner through the test harness. Edit the "
        "spec between scan and gen - that is the intended seam. After it lands, state "
        "PROTOCORE_<MOD>_BORROW in protocore_config.h AND add it to the arena sum: defining it "
        "without summing it is the defect five converted modules carried.",
        ["gen"],
    ),
    "handle": T(
        GOLDENIZE,
        "handle [module.h ...] [--batch=N] [--skip=N] [--dry]",
        "extern <X>Ns <X>; becomes a `static const` table with the operands in <X>V. An extern "
        "table's definition is in another translation unit, so a call through it is INDIRECT and "
        "the compiler cannot read the pointer: measured on sha256 against the RFC 6234 vectors, the "
        "extern form keeps one indirect call and one live symbol PER ENTRY at every optimisation "
        "level, -O2 -flto included, and the static const form emits neither, 316 bytes smaller. "
        "X.entry(work) does not change; only X.foo_args becomes XV.foo_args. "
        "USE --batch. A sweep of all 302 is ONE verdict: it either builds or it does not, and four "
        "thousand errors say nothing about which module caused them. Seven whole-tree attempts ran "
        "4257, 15507, 943, 406, 98, 563, 3771 - it reached one file and then regressed twice. In "
        "batches of 20-30 with a build AND THE SUITE between, the failure names its own module. "
        "RUN THE SUITE, not just the build. The two worst defects both compiled and linked clean: a "
        "table that initialises DATA members in the same brace as its entries (rfc1951 binds "
        ".len_base there) lost them to zero-initialisation and segfaulted in ten suites, and a "
        "CONDITIONAL initializer (network.c guards .dns with #if PROTOCORE_ENABLE_DNS) lost the "
        "guarded members and left them null. A build-only gate ships both. "
        "WHAT THE TREE DOES THAT SHA256 DOES NOT: the object is not named after the file "
        "(protocol.h publishes ConnPool, so the entries are protocore_conn_pool_*); an entry is "
        "bound to a BARE name (.reset = reset), so a guessed <mod>_<entry> renames nothing and the "
        "header declares functions no .c defines; a whole table can sit on ONE line; operands are "
        "written from a macro in the module's own header, from a local alias (HttpClientNs *ns = "
        "&HttpClient), and through a pointer another module holds (SessionV.workers->pump); a "
        "module defines its table once per #if arm. "
        "A NAME IS NOT AUTOMATICALLY FREE: net_addr already publishes protocore_net_addr_to_ip with "
        "another signature, so that module is REPORTED and skipped rather than renamed behind a "
        "caller's back. Read the SKIPPED lines. "
        "TWO CONSEQUENCES THAT ARE NOT BUGS. A test can no longer substitute a module by defining "
        "its namespace object - the header owns that now - so it defines the ENTRY SYMBOLS AND the "
        "Vars instead; forgetting the Vars is an undefined reference to <X>V. And &Namespace is a "
        "different address in every translation unit, so a cross-TU pointer COMPARISON fails where "
        "calling through the pointer still works.",
    ),
    "unnull": T(
        GOLDENIZE,
        "unnull [file.c ...] [--dry]",
        "Removes the `!work` DISJUNCT from every `if` that tests the borrow for null. A BORROW IS "
        "NEVER NULL: it comes from the arena, so the branch cannot be taken and the code it guards "
        "cannot run. "
        "It removes the disjunct, not the guard: `if (!work || !X.args.out)` becomes "
        "`if (!X.args.out)`, because an args member is set by the CALLER and genuinely can be null. "
        "`e->work` and `s->work` are left alone - those name a struct member that happens to be "
        "called work, are not the borrow, and can be null. "
        "An `if` whose ONLY condition was the borrow is REPORTED rather than rewritten: removing it "
        "means deleting the block it guarded, which is a control-flow change to read rather than to "
        "apply in a sweep. "
        "Never add one of these back, and never add one to make a test pass - a test asserting a "
        "null borrow is refused is the stale half of this deletion, so delete the test. "
        "With no paths it sweeps src/. Dry until you drop --dry.",
        ["unnull"],
    ),
    "align": T(
        GOLDENIZE,
        "align [module.h ...] [--dry]",
        "States, per region reached through a CAST, that its offset is a multiple of the alignment "
        "that cast requires. The arena aligns the base up to PROTOCORE_ARENA_MAX_ALIGN, so - in "
        "arena.c's own words - a borrow is met by aligning its offset alone: the offset is the "
        "whole claim, it is a compile-time constant, and so it is a static_assert and NEVER a "
        "runtime branch. "
        "THE SIZE ASSERT ALREADY IN EACH FILE DOES NOT COVER THIS. It bounds the far end of the "
        "chain and says nothing about where any region BEGINS, so one odd-sized region inserted "
        "earlier leaves every later cast misaligned while the total still fits - undefined "
        "behaviour on a part that traps it, a silent slow path on one that does not, and no "
        "diagnostic on either. "
        "A region yielding plain uint8_t* is skipped, and so is a cast to a type whose alignment is "
        "1, where the assert would read `OFF % 1 == 0`. "
        "AN ASSERT THAT FAILS IS THE POINT: it is a misalignment that was already there and had no "
        "diagnostic. Fix it by padding the region ahead of it, never by deleting the assert. "
        "With no paths it sweeps src/. Dry until you drop --dry, like every writing verb here.",
        ["align"],
    ),
    "shape": T(
        GOLDENIZE,
        "shape <file.c|file.h> ... [--dry]",
        "The golden's file shape only: the config include above the enable gate, everything else "
        "below it, so nothing outside the capability compiles. Takes the gate from the spec, not "
        "from the first #if it sees. "
        "RUN BARE IT GUESSES THE GATE, and the guess is wrong in a way that compiles. With no spec "
        "it takes the first `#if PROTOCORE_(ENABLE|NEED|TLS|HAS)_*` in the file, which in "
        "protocol.h is an observability arm around one struct in the middle - hoisting that put the "
        "includes and every type declaration under it, and eleven translation units stopped seeing "
        "ConnState. Read the gate structurally instead (shapeaudit.module_gate: the conditional "
        "whose #endif has nothing after it but the include guard's), and pass it. "
        "AND ONLY MOVE INCLUDES. Moving an #include below the gate is invisible to a consumer - the "
        "header still declares what it declared. Moving a TYPEDEF or a PROTOTYPE is an API change: "
        "mnt.h and http_date.h had declarations above their gates, six files were reading them with "
        "the module disabled, and putting them where the golden puts them cost 3362 errors that "
        "named the types and never the gate. Check what would move before running it on a header.",
        ["shape"],
    ),
    "pimpl": T(
        GOLDENIZE,
        "pimpl <module.h> [--dry]",
        "For a module that is already a namespace but reaches its state through a "
        "struct <X>Internal *ctx. Collapses the storage and the internal into one <X>Ctx at a "
        "compile-time offset in the borrow. Prints the borrow name to state next.",
        ["pimpl"],
    ),
    "funnel": T(
        GOLDENIZE,
        "funnel <module.c> [PROTOCORE_<MOD>_BORROW] [--dry]",
        "Moves a file-static context into the borrow, for a module converted before the funnel "
        "existed - it has the names and not the point. A module keeping its state in a file-static "
        "still passes its tests; it just carries per-module BSS and proves nothing. "
        "A BORROW IS NEVER NULL: it comes from the arena, so `if (!work)` is a dead branch on every "
        "call and 350 of them were deleted from src/ in one commit. Never add one back, and never "
        "add one to make a test pass - a test asserting a null borrow is refused is the stale half "
        "of that deletion, so delete the test. "
        "It reports 'no file-static context to funnel' for two shapes it cannot see: a placement "
        "attribute before the type (`static PSRAM_ATTR XCtx s_x;`) and a pointer bound lazily "
        "(`static struct X *s_x;`). That message is not proof - sweep for file-scope mutables that "
        "are neither a span holder nor a nominal <mod>_work[N].",
        ["funnel"],
    ),
    "audit": T(
        "tools/dev_env/shapeaudit.py",
        "audit [families|check|diff|design|selfcheck] [path ...]",
        "WHICH modules are not the golden, and WHICH question each one answers no to. The golden is "
        "not described in the tool - sha256.h and sha256.c are parsed by the same reader every "
        "target is, so the reference is the file and editing the golden moves every target with it. "
        "`families` groups the tree by the set of checks a module fails, which is how a dry-run "
        "sample is chosen: one module per family stands for the family. `check <module.h>` is the "
        "yes/no list for one module and names the verb each no routes to; `diff` is the trait-level "
        "detail behind it; `design` is the raw record. "
        "`selfcheck` asserts the GOLDEN answers yes to every check - run it after touching a check, "
        "because a check the golden fails marks all 400 modules divergent on a question the target "
        "of the conversion does not itself satisfy. "
        "SELFCHECK IS NOT ENOUGH, and a green one is what hid the worst bug this tool has had: a "
        "check that compares a COUNT to the golden's count passes selfcheck by construction and "
        "fails every module of a different size. The golden has four entries, so `no flat decls` "
        "read 328 modules as divergent for having more, and `functions placed` 334. A check must "
        "compare what survives renaming and resizing - a NAME the table does or does not bind, a "
        "path that is or is not the config - and `shapeaudit_test.py` pins that with a synthetic "
        "six-entry module which must answer yes. Before believing a number here, read one module "
        "the check names and confirm it is actually wrong. "
        "A check that cannot apply reports n/a, never no: a header with no .c answers nothing about "
        "an implementation, and a module naming no borrow has no subject for the three questions "
        "about how the borrow is carved. Read the n/a column - it is not a pass.",
    ),
    "nsmap": T(
        "tools/dev_env/nsmap.py",
        "nsmap <map.json> [--dry] [path ...]",
        "Rewrites the leftover FLAT call sites of an already-golden module onto its namespace, "
        'from a stated map. Paths default to the map\'s "files". It refuses rather than guesses: '
        "a loop condition (hoisting evaluates it once and the loop spins), a call inside a macro "
        "that re-evaluates its argument, two calls to one namespace in one statement (one result "
        "member, so the first value is overwritten), and an argument count the map does not "
        "state. Each refusal is reported with its line; convert those by hand into the comma form "
        "(Ns.entry(work), read). "
        "THIS IS THE TOOL FOR REWRITING CALL SITES - never hand-roll a sed or a Python pass over "
        "them. A blind rename cannot see a literal, a macro that re-evaluates, or a braceless "
        "if/else body, and every one of those changes behaviour while still compiling. "
        'State "ctx" or the borrow defaults to <Object>.internal, which most modules do not have; '
        "it is usually protocore_<mod>_span(). Where the flat form took a caller-declared handle "
        "that is now the borrow, drop that argument first - the arity has to match the map or every "
        'call is reported instead of rewritten. "seed" states the members the flat signature never '
        "carried.",
    ),
}

EDIT = {
    "move": T(
        "tools/dev_env/move_code.py",
        "move --src A --dst B --range N-M [--anchor-before RE|--anchor-after RE|--append] [--dry-run]",
        "Ranges are 1-indexed, inclusive, and read from the ORIGINAL numbering, so several "
        "--range flags at once do not shift each other. --expect-start / --expect-end are regexes "
        "checked against the first and last line before anything is written, and a failed guard "
        "aborts with no change. Use them. "
        "--src and --dst the SAME file is a silent no-op: the ranges are cut from the original and "
        "spliced into the original independently, the last write wins, and nothing is reported. "
        "Move a function within one file by hand.",
    ),
    "yank": T(
        "tools/dev_env/yank_includes.py",
        "yank PATH [PATH ...] [--conditional] [--orphan] [--go]",
        "Takes the #include lines out of a header and records, per consumer, what that header no "
        "longer carries. Dry run by default; --go rewrites and writes test/yanked_includes.json, "
        "which `build cmake` turns into a forced include on exactly the source file that owes it - "
        "so the dependency moves into the build instead of being handed to every consumer. Skips "
        "the entry-point chain, conditional includes, and a header nothing reaches.",
    ),
    "comments": T(
        "tools/dev_env/strip_comments.py",
        "comments PATH [PATH ...] [--ext .c,.h] [--exclude PAT] [--no-header] [--go]",
        "Dry run by default; --go rewrites in place. The licence header and every string literal "
        "survive, and a block comment leaves its newlines so a compiler error still points at the "
        "right line. This is what makes a line-anchored conversion mechanical: prose is what a "
        "pattern over source lines trips over.",
    ),
}

VIEW = {
    "read": T(
        "tools/dev_env/readclean.py",
        "read code|blind|claims PATH [PATH ...]   [--legend FILE] [--keep NAME,...]",
        "READ EVERY MODULE THROUGH THIS BEFORE JUDGING IT. Three passes, none of which writes. "
        "`blind` first: comments gone AND every name this project chose replaced, so the code is "
        "read for what it does rather than for what it is called - a function named `verify` is "
        "read as verifying, and that is the one claim nothing ever checks. The shape's own grammar "
        "survives (an entry still takes `uint8_t *restrict work`, offsets still sit under a "
        "static_assert), so conformity is still checkable while the module's identity is not "
        "visible. Then `code` for the structure with names attached, and `claims` LAST, which pairs "
        "every comment with the line it sits above so prose is met with its subject instead of "
        "being read as if it were true. Assume nothing: a doc block states what the code is MEANT "
        "to do, and reading it first is how a conformity pass ends up confirming the prose.",
    ),
    "png": T(
        "tools/dev_env/src2png.py",
        "png <file> <out_stem> [lines_per_page] [pt] [start] [end]   |   png <dir> <dest> [kb_per_page] [pt]",
        "Renders source to numbered PNG pages, for reading a whole file at image density. Pages "
        "break on whole lines, so a line never splits across two.",
    ),
    "nodeset": T(
        "tools/dev_env/nodeset.py",
        "nodeset <NodeSet2.xml> [BrowseName ...]",
        "What one NodeSet2 file publishes for each node. Flat read of the file - it does NOT "
        "follow instance-of or subtype-of, so use `conform` to compare a module against a model.",
    ),
    "tree": T(
        "tools/dev_env/uatree.py",
        "tree <root BrowseName> <depth> <NodeSet2.xml> [more.xml ...]",
        "The address space as a tree, with instance-of and subtype-of followed. A NodeSet declares "
        "a container's children on its TYPE and a type inherits its base type's children, so "
        "reading a file without following those two links makes almost every real edge look "
        "missing.",
    ),
    "conform": T(
        "tools/dev_env/opcua_conform.py",
        "conform <module.c> <NodeSet2.xml> [more.xml ...]",
        "Checks a hand-built companion-model address space against the NodeSets that publish it. "
        "Pass EVERY NodeSet the model depends on: a node resolves to the model that owns it by "
        "(namespace uri, numeric id), and a missing file reads as a missing node.",
    ),
}

MEASURE = {
    "includes": T(
        "tools/include_footprint.py",
        "includes [--check] [--issues] [--json] [--per-file] [--src PATH]",
        "The std/Arduino header dependency surface of src/. --check is the CI form.",
    ),
    "pid": T(
        "tools/pid_tune.py",
        "pid [--autotune] [--sweep] [--kp K --ki K --kd K] [--out-min N --out-max N] [--png PATH]",
        "Offline tuner and plotter for services/control, fitted from a run log. Nothing is on the "
        "target here - it produces gains to state, not gains it installed.",
    ),
    "queue-probe": T(
        "tools/dev_env/listener_queue/run.sh",
        "queue-probe",
        "Standing probe: is the TCP listener queue unguarded, or guarded to WORKER_COUNT == 1. "
        "A static count of the sites that reach it, plus a runtime probe. Does not build the "
        "library.",
    ),
    "cap-sweep": T(
        "tools/dev_env/capsweep.py",
        "cap-sweep [PROTOCORE_HAS_...] [--quiet]",
        "Compiles every capability-gated module with the capability OFF, which nothing else does: "
        "test/core_setup/hal/host answers every hardware question one way, so the other arm of each "
        "is text no target includes and a green matrix says nothing about it. Two answers are "
        "correct and it does not rank them - `refuses` (an #error, because the module IS the "
        "hardware) and `compiles` (a stand-in exists and builds). `BROKEN` is the defect: an arm "
        "meant to stand in that does not build. It compiles with the capability ON first as a "
        "control, so a missing dependency reads as n/a rather than as a rotted arm, and it takes "
        "each file's flags from test_matrix.json rather than guessing them. A module answering "
        "`compiles` wants an env building it that way, or the arm goes back to being uncompiled "
        "text - see native_hmmd_nobus.",
    ),
    "pimpl-sweep": T(
        "tools/dev_env/pimpl_bench/sweep.sh",
        "pimpl-sweep",
        "Does an opaque cross-TU accessor get inlined, and at what setting. Builds the whole "
        "program per (compiler, -O level, lto), disassembles the consumer's hot loop and counts "
        "call instructions against total instructions. Answers whether the call survives.",
    ),
    "pimpl-bench": T(
        "tools/dev_env/pimpl_bench/bench.sh",
        "pimpl-bench",
        "What that accessor costs in time, across the same flag matrix. Minimum over trials, so "
        "the number is the run least disturbed by scheduling. sweep says whether the call "
        "survives; this says what it is worth.",
    ),
}

CRYPTO = {
    "kat": T(
        "tools/crypto/gen_crypto_vectors.py",
        "kat",
        "Rewrites the MAC/AEAD known-answer data the crypto KAT suite compiles in "
        "(test/unit/src/crypto/mac/test_crypto_kat/kat_data.inc).",
    ),
    "curate": T(
        "tools/crypto/curate_crypto_vectors.py",
        "curate [checkout]",
        "Pulls vectors out of a pinned upstream checkout into test/vectors/. With no argument it "
        "clones the pin itself, so the set is reproducible rather than whatever was on disk.",
    ),
    "ed25519": T(
        "tools/crypto/gen_ed25519_comb.py",
        "ed25519",
        "Generates AND verifies the fixed-base comb table for the Ed25519 fe layer against an "
        "affine reference. Python is the oracle before any C is written.",
    ),
    "mlkem": T(
        "tools/crypto/gen_mlkem_kat.py",
        "mlkem",
        "A deterministic ML-KEM-768 Encaps known-answer vector, as a C header for test_pqc_mlkem.",
    ),
    "pss": T(
        "tools/crypto/gen_rsa_pss_vectors.py",
        "pss",
        "RSASSA-PSS signatures over the key openssl_rsa_2048_sign.json already publishes, signed by "
        "openssl. PSS draws a random salt, so a signature cannot be recomputed and compared - only a "
        "verifier can check one, which is why the signer has to be outside this tree. Needs openssl "
        "on PATH. Run `crypto kat` after it to recompile the .inc the suite reads.",
    ),
    "hkdf384": T(
        "tools/crypto/gen_hkdf_sha384_vectors.py",
        "hkdf384 [--check]",
        "HKDF-SHA384 and TLS 1.3 HKDF-Expand-Label answers from openssl, over the RFC 5869 Appendix A "
        "inputs. RFC 5869 tabulates HKDF only for SHA-256 and SHA-1 and RFC 8448's trace is SHA-256 "
        "throughout, so there is no published SHA-384 vector to read. It calibrates first: --check "
        "re-runs the SHA-256 forms against RFC 5869 A.1 and the RFC 8448 sec 3 'derived' secret and "
        "writes nothing if either disagrees. Needs openssl on PATH. Run `crypto kat` after it to "
        "recompile the .inc the suite reads.",
    ),
    "tls": T(
        "tools/crypto/gen_tls_record_kat.py",
        "tls",
        "A TLS 1.3 record-layer known-answer vector, as C arrays for test_tls_record.",
    ),
    "sshkeys": T(
        "tools/crypto/gen_ssh_test_keys.py",
        "sshkeys [--if-absent]",
        "Mints the SSH test keys into the fixture header. --if-absent is the form a build hook "
        "wants: it leaves an existing header alone rather than churning the tree on every run. "
        "Needs openssl on PATH.",
    ),
    "sshhost": T(
        "tools/crypto/gen_ssh_host_key.py",
        "sshhost [--type ed25519] [--name N] [--symbol S] [--out-dir D] [--header H]",
        "One SSH host key as a C header. Needs openssl and ssh on PATH.",
    ),
    "sshinflate": T(
        "tools/crypto/gen_ssh_inflate_vectors.py",
        "sshinflate",
        "SSH c2s compression golden vectors, produced the way OpenSSH does it: one zlib stream "
        "with context takeover, Z_PARTIAL_FLUSH per packet. The cross-packet back-references are "
        "the point - a per-packet stream would pass a test that proves nothing.",
    ),
    "x509": T(
        "tools/dev_env/gen_x509_fixture.sh",
        "x509",
        "Makes real certificates with OpenSSL and turns them into the X.509 suite's fixture "
        "header, reading the EXPECTED values back out of OpenSSL too. A suite whose author "
        "supplies both the input and the answer only proves the author is consistent with "
        "themselves. gen_x509_fixture.py is the second half and is not run directly.",
    ),
}

ASSETS = {
    "diagrams": T(
        "tools/ci_tooling/assets/render_diagrams.sh",
        "diagrams",
        "Renders each docs/diagrams/ source to a light AND a dark PNG, which the docs embed "
        "through <picture>. A live mermaid fence is not an option: the GitHub mobile app and "
        "Doxygen do not render mermaid. Needs mmdc.",
    ),
    "themes": T(
        "tools/ci_tooling/assets/render_theme_previews.cjs",
        "themes",
        "Screenshots every theme in src/web_assets/themes/ onto one sample page, into "
        "docs/theme_preview/. Needs node with puppeteer-core and a Chromium.",
    ),
    "favicons": T(
        "tools/ci_tooling/assets/pack_favicons.sh",
        "favicons <name>",
        "Packs one generated favicon SVG into a drop-in tarball under docs/favicons/dist/. Needs "
        "ImageMagick's convert.",
    ),
    "tooltips": T(
        "tools/ci_tooling/assets/svg_tooltips.py",
        "tooltips <file.svg> ...",
        "Turns mermaid's click tooltips into real SVG tooltips, which is the only form that "
        "survives being embedded as an image.",
    ),
}

BUILD = {
    "cmake": T(
        "tools/ci_tooling/build/gen_cmake.py",
        "cmake [--check]",
        "Writes test/CMakeLists.txt from test/test_matrix.json: one executable target and one ctest "
        "per env, compiled the way test/harness.py compiles it. The matrix stays the one statement "
        "of what an env builds; this renders it. --check fails on a stale file and writes nothing.",
    ),
    "split": T(
        "tools/ci_tooling/build/split_modules.py",
        "split [--list] [--go]",
        "Gives every .c/.h pair its own directory, so a module is a directory and a per-module "
        "CMakeLists.txt has somewhere to live. Only directories holding more than one pair are "
        "touched. Every include spelling that names a moved header - repo-relative, bare from a "
        "sibling, dotted from a child - and every build_src_filter entry moves with it. Dry run "
        "by default.",
    ),
    "modules": T(
        "tools/ci_tooling/build/gen_modules.py",
        "modules [--check|--graph|--unowned]",
        "Writes src/CMakeLists.txt: one CMake target per module, with the dependencies read out of "
        "what each module includes rather than maintained by hand. Include paths and transitive deps "
        "then propagate through the targets, and a module whose PROTOCORE_ENABLE_* gate is off is a "
        "target that is never added instead of a file that compiles to nothing. "
        "THIS IS HALF THE BUILD. It renders WHICH MODULE compiles; `build cmake` renders WHAT "
        "DEFINES each test target compiles with, from the same matrix. A flag change needs both, and "
        "running only this one leaves every target on its previous define set - which reads as the "
        "source being wrong rather than the build being stale. `test/harness.py env gen` is a third "
        "renderer of the same matrix, for platformio.ini.",
    ),
    "envs": T(
        "tools/dev_env/build_envs.sh",
        "envs <env> [env ...]",
        "Builds each named pio env in the WSL clone and prints one verdict per env. WSL sshd does "
        "not survive a cold start - `sudo systemctl start ssh` first if this is reaching WSL over "
        "ssh.",
    ),
    "ccache": T(
        "tools/ci_tooling/build/ccache_wrap.sh",
        "ccache",
        "Replaces the cross-toolchain's gcc/g++ with a ccache wrapper in place. PlatformIO and "
        "arduino-cli invoke the ESP32 compiler by ABSOLUTE path, so a PATH masquerade is bypassed "
        "and only replacing the tool works.",
    ),
    "psram": T(
        "tools/psram/rebuild_arduino_core_psram.sh",
        "psram",
        "Rebuilds the arduino-esp32 core for ESP32-S3 with "
        "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y, which is what lets a static "
        "EXT_RAM_BSS_ATTR array land in PSRAM. Long; needs arduino-cli, cmake and git.",
    ),
}

HOOKS = {
    "commit": T(
        "tools/dev_env/commit.py",
        'commit "subject" "para" ["para" ...] [--amend] [--no-signoff] [--dry] [-- <git args>]',
        "QUOTE EACH PARAGRAPH WITH SINGLE QUOTES. A message is prose about code, so it carries "
        "backticks and $ signs, and inside a double-quoted shell argument the shell runs them before "
        "this tool sees anything: a paragraph naming `work` and `scratch` was committed with both "
        "words replaced by nothing and 'command not found' on stderr. Single quotes pass the bytes "
        "through. "
        "Commits a multi-paragraph message with no file to hold it: each argument after the subject "
        "is one paragraph, and the assembled message goes to `git commit -F -` down a PIPE. That is "
        "the whole point - a `-m` carrying embedded newlines is a heredoc, and a message written to "
        "a temp file is a file to remember to delete. "
        "THE TRAILER IS THE COMMITTER'S AND ONLY THE COMMITTER'S: git config user.name / "
        "user.email, the same source `git commit -s` reads, with a parenthetical in the name "
        "dropped so it matches the handle already in this log. No flag names a different author, "
        "and that is deliberate. "
        "A paragraph that is indented or opens a list, table or quote is emitted VERBATIM - only "
        "prose is re-wrapped, so a command example keeps its shape and a wrapped `git commit ...` "
        "does not become two broken halves. --dry prints the message and the git command and "
        "commits nothing.",
    ),
    "install": T(
        None,
        "install",
        "Points git at tools/git-hooks by setting core.hooksPath, so the hooks are the tracked "
        "ones rather than copies that drift. Idempotent.",
    ),
    "status": T(
        None,
        "status",
        "What core.hooksPath is set to now, and which hook files that path actually holds.",
    ),
    "cspell": T(
        "tools/git-hooks/add_cspell_words.py",
        "cspell <word> [word ...]",
        "Adds words to cspell.json with case-insensitive dedup, keeping the list sorted and the "
        "file's 2-space indentation. The pre-commit hook calls this so an unknown technical term "
        "in committed docs never fails the CI spellcheck.",
    ),
    "dependabot": T(
        "tools/git-hooks/merge_dependabot.sh",
        "dependabot",
        "Merges open, mergeable Dependabot PRs into the current branch. Best-effort and strictly "
        "non-blocking: it does nothing, and never fails its caller, when gh is missing or "
        "unauthenticated, when HEAD is detached, or mid-rebase.",
    ),
}

SELFTESTS = {
    "goldenize": "tools/dev_env/goldenize_test.py",
    "nsconv": "tools/dev_env/nsconv_test.py",
    "nsmap": "tools/dev_env/nsmap_test.py",
    "pimpl": "tools/dev_env/pimpl_test.py",
    "funnel": "tools/dev_env/funnel_test.py",
    "shapeaudit": "tools/dev_env/shapeaudit_test.py",
    "readclean": "tools/dev_env/readclean_test.py",
}

# The dispatch groups, in the order `list` and `help` print them.
GROUPS = [
    ("convert", CONVERT, "flat C module -> the sha256 golden shape, and the call sites with it"),
    ("edit", EDIT, "mechanical source edits that a pattern-driven conversion needs first"),
    (
        "view",
        VIEW,
        "read-only readers; none of these writes into the tree. START WITH `view read blind` - assume nothing, evaluate everything, and meet the prose last",
    ),
    ("measure", MEASURE, "what something costs, and the three standing probes"),
    ("crypto", CRYPTO, "test vectors and keys - test DATA, which is why it is not under ci_tooling"),
    ("assets", ASSETS, "renders images and packs web assets"),
    ("hooks", HOOKS, "git: the tracked hooks, pointing git at them, and committing through them"),
    ("build", BUILD, "build environments and toolchain wrappers"),
]

TABLES = dict((name, table) for name, table, _blurb in GROUPS)


# ---------------------------------------------------------------------------
# running one tool
# ---------------------------------------------------------------------------


def run_py(path, argv):
    """Run one Python tool in-process, as if it had been invoked directly.

    `python x.py` puts the script's own directory on sys.path and runpy does not, so a tool that
    imports a sibling (uatree -> uaspace, goldenize -> pimpl) fails under run_path without this.
    """
    here = os.path.dirname(path)
    saved_argv, saved_path = sys.argv, list(sys.path)
    sys.argv = [os.path.basename(path)] + list(argv)
    for entry in (ROOT, here):
        if entry not in sys.path:
            sys.path.insert(0, entry)
    try:
        runpy.run_path(path, run_name="__main__")
        return 0
    except SystemExit as e:
        if e.code is None:
            return 0
        if isinstance(e.code, int):
            return e.code
        print(e.code, file=sys.stderr)
        return 1
    finally:
        sys.argv = saved_argv
        sys.path[:] = saved_path


GIT_BASH = r"C:\Program Files\Git\bin\bash.exe"


def find_bash():
    """Git Bash before WSL's bash.

    On Windows `bash` resolves to System32\\bash.exe, which is WSL: it takes Linux paths, so a
    Windows path handed to it is a file that does not exist and the script dies on its first line.
    """
    found = shutil.which("bash")
    if found and "system32" not in found.replace("/", "\\").lower():
        return found
    if os.path.exists(GIT_BASH):
        return GIT_BASH
    return found


def run_exe(exe, install, path, argv):
    """Run a tool that is not Python, through the interpreter it needs."""
    found = find_bash() if exe == "bash" else shutil.which(exe)
    if not found:
        print("%s is not on PATH, and %s needs it: %s" % (exe, findroot.rel(path), install), file=sys.stderr)
        return 127
    return subprocess.run([found, path] + list(argv), cwd=ROOT).returncode


def run_tool(tool, rest):
    """Dispatch one table entry by what it is."""
    if tool.path is None:
        return tool.fn(rest)
    path = findroot.at(*tool.path.split("/"))
    if not os.path.exists(path):
        print("missing: %s (the table names it, the tree does not have it)" % tool.path, file=sys.stderr)
        return 2
    argv = list(tool.argv) + list(rest)
    ext = os.path.splitext(path)[1]
    if ext == ".py":
        return run_py(path, argv)
    if ext == ".cjs" or ext == ".js":
        return run_exe("node", "install Node, then `npm i puppeteer-core`", path, argv)
    return run_exe("bash", "install Git for Windows, or run this from WSL", path, argv)


def group_runner(table):
    def run(a):
        return run_tool(table[a.name], a.rest)

    return run


# ---------------------------------------------------------------------------
# hooks: the two entries that are this file rather than a script
# ---------------------------------------------------------------------------

HOOKS_PATH = "tools/git-hooks"


def git(*args):
    return subprocess.run(["git"] + list(args), cwd=ROOT, capture_output=True, text=True)


def cmd_hooks_install(rest):
    """Point git at the tracked hooks."""
    now = git("config", "core.hooksPath").stdout.strip()
    if now == HOOKS_PATH:
        print("core.hooksPath is already %s" % HOOKS_PATH)
        return 0
    r = git("config", "core.hooksPath", HOOKS_PATH)
    if r.returncode:
        print(r.stderr.strip(), file=sys.stderr)
        return r.returncode
    print("core.hooksPath: %s -> %s" % (now or "(unset)", HOOKS_PATH))
    return 0


def cmd_hooks_status(rest):
    """What git is pointed at, and what is there."""
    now = git("config", "core.hooksPath").stdout.strip()
    print("core.hooksPath = %s" % (now or "(unset - git uses .git/hooks)"))
    d = findroot.at(*HOOKS_PATH.split("/"))
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            print("  %-22s %s" % (name, "executable" if os.access(p, os.X_OK) else "not executable"))
    if now != HOOKS_PATH:
        print("\nnot installed: harness.py hooks install")
    return 0


HOOKS["install"].fn = cmd_hooks_install
HOOKS["status"].fn = cmd_hooks_status


# ---------------------------------------------------------------------------
# what the tree holds: derived from the code, not from the docstrings
# ---------------------------------------------------------------------------

# A write primitive: the file can change the tree. Deleting a build artifact is not one - a tool
# that only unlinks its own .gcda is read-only as far as the tree is concerned.
PY_WRITE = re.compile(
    r"""open\s*\([^)]*?["'][wax]b?\+?["']|\.write_text\(|\.write_bytes\(|json\.dump\s*\(|"""
    r"""shutil\.(?:copy|copy2|copyfile|copytree|move)\s*\(|os\.(?:replace|rename)\s*\("""
)
SH_WRITE = re.compile(r"(?m)(?:^|[^0-9<>&])>>?\s*[^&\s]|(?:^|[|;&(]\s*|\s)(?:cp|mv|tee|install|mkdir)\s")

# Flags the file itself READS. A flag it merely hands to something else is that tool's flag, so
# only a declaration or a comparison counts: argparse declares one, and `!=` / `in argv` reads one.
PY_FLAG = re.compile(
    r"""add_argument\(\s*["'](--[a-z0-9][a-z0-9-]*)["']"""
    r"""|["'](--[a-z0-9][a-z0-9-]*)["']\s*(?:not\s+in|in|==|!=)"""
    r"""|(?:not\s+in|in|==|!=)\s*["'](--[a-z0-9][a-z0-9-]*)["']"""
    r"""|argv\.(?:remove|count|index)\(\s*["'](--[a-z0-9][a-z0-9-]*)["']"""
)
SH_FLAG = re.compile(r"(?m)^\s*(--[a-z0-9][a-z0-9-]*)\)|\[\s*\"\$\w+\"\s*=\s*\"?(--[a-z0-9][a-z0-9-]*)")

# Commands a Python file hands to the OS.
PY_EXEC = re.compile(
    r"""subprocess\.(?:run|call|check_call|check_output|Popen)\(\s*\[?\s*["']([^"']+)["']"""
    r"""|shutil\.which\(\s*["']([^"']+)["']"""
    r"""|\brun\(\s*\[\s*["']([^"']+)["']"""
)

# A shell script names its commands bare, so the column is filtered to what is actually external.
EXTERNALS = {
    "arduino-cli",
    "awk",
    "bash",
    "black",
    "cc",
    "ccache",
    "clang",
    "clang++",
    "clang-format",
    "cmake",
    "convert",
    "cspell",
    "curl",
    "doxygen",
    "g++",
    "gcc",
    "gcovr",
    "gh",
    "git",
    "gpg",
    "idf.py",
    "lcov",
    "make",
    "mmdc",
    "node",
    "npm",
    "npx",
    "objdump",
    "openssl",
    "pio",
    "platformio",
    "prettier",
    "python",
    "python3",
    "qemu-system-riscv32",
    "qemu-system-xtensa",
    "rsync",
    "ruby",
    "scp",
    "sed",
    "shfmt",
    "ssh",
    "tar",
    "wsl.exe",
    "xxd",
    "zip",
}
SH_WORD = re.compile(r"(?m)(?:^|[|;&(]|\$\(|`|\bthen\b|\bdo\b|&&|\|\|)\s*([A-Za-z][\w.+-]*)")
# A shell script names the tool three ways: at a command position, held in a variable, and listed
# in a for. pimpl_bench loops `for CC in gcc cc`, build_envs holds pio in PIO="$HOME/.../pio", and
# compile_examples holds the whole ssh command line in one local.
SH_ASSIGN = re.compile(r"(?m)^\s*(?:local\s+|export\s+|declare\s+)?[A-Za-z_]\w*=[\"']?([^\"'\n]*)")
SH_FOR = re.compile(r"(?m)\bfor\s+\w+\s+in\s+([^;\n]+)")


def sh_commands(src):
    """Every external command a shell script names, however it names it."""
    words = list(SH_WORD.findall(src))
    for value in SH_ASSIGN.findall(src):
        head = value.split()[0] if value.split() else ""
        words.append(head.rsplit("/", 1)[-1])
    for group in SH_FOR.findall(src):
        words += group.split()
    return [w for w in words if w in EXTERNALS]


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def header_comment(src):
    """The leading comment block, minus the shebang, the copyright and the SPDX line."""
    lines = []
    for line in src.split("\n")[:16]:
        s = line.strip()
        if not s.startswith("#") and not s.startswith("//"):
            if lines:
                break
            continue
        s = s.lstrip("#/ ").strip()
        if not s or s.startswith("!") or "Copyright (C)" in s or s.startswith("SPDX"):
            continue
        lines.append(s)
    return " ".join(lines)


def first_line(path, src):
    """The file's own one-line summary: its docstring's first sentence, or its header comment.

    A file with neither says nothing about itself, and prints blank rather than borrowing a
    sentence from somewhere else.
    """
    text = ""
    if path.endswith(".py"):
        try:
            text = ast.get_docstring(ast.parse(src)) or ""
        except SyntaxError:
            text = ""
        text = text.strip().split("\n\n")[0]
    if not text.strip():
        text = header_comment(src)
    text = " ".join(text.split())
    # A doxygen-style docstring opens with the tags, and the tags are not the summary.
    text = re.sub(r"^@file\s+\S+\s*", "", text)
    text = re.sub(r"^@brief\s*", "", text)
    # A summary that names the file first says nothing the path did not.
    text = re.sub(r"^[\w.]+\.(?:py|sh|cjs)\s*-\s*", "", text)
    return text


def scan_file(path):
    """What one file is, read out of the file: does it write, what flags does it take, what does
    it shell out to."""
    src = read(path)
    rel = findroot.rel(path)
    py = path.endswith(".py")
    if py:
        writes = bool(PY_WRITE.search(src))
        flags = [g for m in PY_FLAG.finditer(src) for g in m.groups() if g]
        shells = [g for m in PY_EXEC.finditer(src) for g in m.groups() if g]
        if re.search(r"sys\.executable", src):
            shells.append("python")
    else:
        writes = bool(SH_WRITE.search(src))
        flags = [g for m in SH_FLAG.finditer(src) for g in m.groups() if g]
        shells = sh_commands(src)
    shells = [os.path.basename(s) for s in shells if "/" not in s and "\\" not in s]
    shells = [s for s in shells if s in EXTERNALS or s.endswith("-cli")]
    return {
        "rel": rel,
        "name": os.path.basename(path),
        "writes": writes,
        "flags": sorted(set(flags)),
        "shells": sorted(set(shells)),
        "runnable": py and "__main__" in src or not py,
        "doc": first_line(path, src),
    }


# The directory sections `list --all` and `doc gen` print, in order, each with what the directory
# is for and the one note a reader needs about it.
DIRS = [
    (
        "tools",
        "Top level",
        "The entry point, the root finder, and the two measurements that " "belong to no family.",
        "",
    ),
    (
        "tools/ci_tooling",
        "ci_tooling",
        "One entry point for CI: `harness.py ci` hands off to it "
        "unread, so `harness.py ci help` is its whole surface.",
        "",
    ),
    (
        "tools/ci_tooling/check",
        "check/ - fails CI, writes nothing",
        "",
        "`check_frame_specs.py` writes only under `--fix`. `compile_examples.sh` builds on a remote "
        "host. Every guard here is reachable as `harness.py ci check <name>`.",
    ),
    (
        "tools/ci_tooling/generate",
        "generate/ - writes into tracked files",
        "",
        "Every one takes `--check` to assert the tracked file already matches, which is how CI "
        "detects drift. Reachable as `harness.py ci gen <name>`.",
    ),
    (
        "tools/ci_tooling/coverage",
        "coverage/",
        "",
        "`covrun.py` is the inner loop over a few envs; `covbase.py` is the full-matrix rebuild. "
        "Reachable as `harness.py ci cov <name>`.",
    ),
    ("tools/ci_tooling/lib", "lib/ - imported, never run", "", ""),
    ("tools/ci_tooling/sonar", "sonar/", "", ""),
    ("tools/ci_tooling/assets", "assets/", "", ""),
    ("tools/ci_tooling/build", "build/", "", ""),
    ("tools/crypto", "crypto/ - generates test vectors and keys", "", ""),
    (
        "tools/dev_env",
        "dev_env/ - the conversion tools and the readers",
        "",
        "`nsconv.py`, `codemask.py`, `uaspace.py`, `pimpl.py` and `funnel.py` are libraries: "
        "`goldenize.py` is the entry point for the last two. `gen_x509_fixture.py` is the second "
        "half of `gen_x509_fixture.sh`.",
    ),
    ("tools/dev_env/listener_queue", "dev_env/listener_queue/", "", ""),
    ("tools/dev_env/pimpl_bench", "dev_env/pimpl_bench/", "", ""),
    (
        "tools/git-hooks",
        "git-hooks/",
        "",
        "Installed by pointing git at the directory (`harness.py hooks install`), never by copying "
        "into .git/hooks, so what runs is what is tracked.",
    ),
    ("tools/psram", "psram/", "", ""),
]

SKIP_FILES = {"__init__.py"}
SCAN_EXTS = (".py", ".sh", ".cjs")


def inventory():
    """Every script under tools/, grouped by directory, in DIRS order."""
    out = []
    for rel, title, blurb, note in DIRS:
        d = findroot.at(*rel.split("/"))
        if not os.path.isdir(d):
            continue
        files = []
        for name in sorted(os.listdir(d)):
            p = os.path.join(d, name)
            if not os.path.isfile(p) or name in SKIP_FILES:
                continue
            if not name.endswith(SCAN_EXTS) and name not in ("pre-commit", "post-commit"):
                continue
            files.append(scan_file(p))
        if files:
            out.append((rel, title, blurb, note, files))
    return out


def md_table(rows, headers):
    """A markdown table padded so the source is readable without a renderer."""
    cols = list(zip(*([headers] + rows))) if rows else [(h,) for h in headers]
    width = [max(len(str(c)) for c in col) for col in cols]

    def line(cells):
        return "| " + " | ".join(str(c).ljust(w) for c, w in zip(cells, width)) + " |"

    out = [line(headers), "| " + " | ".join("-" * w for w in width) + " |"]
    out += [line(r) for r in rows]
    return "\n".join(out)


def render_inventory():
    """The derived half of TOOLS.md."""
    out = []
    for _rel, title, blurb, note, files in inventory():
        out.append("## %s" % title)
        if blurb:
            out.append("")
            out.append(blurb)
        rows = [
            [
                "`%s`" % f["name"],
                "W" if f["writes"] else "",
                "`%s`" % " ".join(f["flags"]) if f["flags"] else "",
                ", ".join(f["shells"]),
            ]
            for f in files
        ]
        out.append("")
        out.append(md_table(rows, ["Script", "W", "Flags", "Shells out to"]))
        if note:
            out.append("")
            out.append(note)
        out.append("")
    return "\n".join(out).strip()


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------


def wrap(text, indent, width=98):
    return textwrap.fill(text, width=width, initial_indent=" " * indent, subsequent_indent=" " * indent)


def table_help(table):
    """One tool per entry: how it is called, then the hint."""
    out = []
    for name, tool in table.items():
        out.append("  %s" % tool.usage)
        out.append(wrap(tool.hint, 6))
        out.append("")
    return "\n".join(out)


def cmd_list(a):
    """Every runnable tool, grouped by what it does; --all adds the whole tree by directory."""
    if a.group:
        if a.group not in TABLES:
            print("no such group: %s (groups: %s)" % (a.group, " ".join(TABLES)), file=sys.stderr)
            return 2
        print(table_help(TABLES[a.group]).rstrip())
        return 0
    for name, table, blurb in GROUPS:
        print("\nharness.py %s - %s" % (name, blurb))
        for tool, entry in table.items():
            print("  %-12s %-42s %s" % (tool, entry.path or "(this file)", entry.usage))
    print("\nharness.py ci <...>  - tools/ci_tooling/ci.py: gen, check, baseline, cov, fmt, sonar")
    print("harness.py selftest  - %s" % " ".join(SELFTESTS))
    if not a.all:
        print("\n`harness.py list --all` prints every file under tools/, derived from the code.")
        return 0
    for _rel, title, _blurb, _note, files in inventory():
        print("\n%s" % title)
        wide = max(len(f["name"]) for f in files)
        for f in files:
            mark = "W" if f["writes"] else " "
            print("  %s %-*s  %s" % (mark, wide, f["name"], f["doc"][: 96 - wide]))
    return 0


def cmd_ci(a):
    """Hand off to tools/ci_tooling/ci.py with whatever followed `ci`.

    Same reason test/harness.py hands `bare` and `bench` off to their own files: CI operations are
    their own matrix with their own surface, and nothing should have to know that to run them. Its
    own help is the authority - `harness.py ci help`.
    """
    return run_py(findroot.at("tools", "ci_tooling", "ci.py"), a.rest or ["help"])


def cmd_selftest(a):
    """Run the tools' own self-tests. Each is a defect the tool shipped once."""
    names = a.names or list(SELFTESTS)
    unknown = [n for n in names if n not in SELFTESTS]
    if unknown:
        print("unknown self-test: %s (have: %s)" % (" ".join(unknown), " ".join(SELFTESTS)), file=sys.stderr)
        return 2
    rc_all = 0
    for name in names:
        path = findroot.at(*SELFTESTS[name].split("/"))
        print("\n=== %s" % findroot.rel(path))
        rc = run_py(path, [])
        print("%s %s" % ("ok  " if rc == 0 else "FAIL", name))
        rc_all = rc_all or rc
    return rc_all


def doc_gen(check=False):
    """Rewrite the derived tables in tools/TOOLS.md from the code.

    The "I need to..." table and the prose above the region are hand-written and stay that way;
    everything below it is what the files actually are. `check` is the CI form.

    ci_tooling/generate/gen_tools_inventory.py is the same call under the name CI knows it by, so
    the region is gated the way every other generated region is.
    """
    sys.path.insert(0, findroot.at("tools", "ci_tooling", "lib"))
    import doc_region as dr  # noqa: E402  (path set above)

    region = dr.Region(DOC, "TOOLS INVENTORY", findroot.here(__file__))
    return dr.apply(DOC, {region: render_inventory()}, check=check)


def cmd_doc_gen(a):
    return doc_gen(check=a.check)


def _subcommands(parser):
    """{name: subparser} for a parser's one subparser group."""
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action.choices
    return {}


def cmd_help(a):
    """Every command's own help in one call, or one command's.

    `-h` prints usage one level at a time, so reading the whole surface means invoking it once per
    subcommand. This prints all of it, and the inventory after it, so one call is the whole tool.
    """
    subs = _subcommands(build_parser())
    if a.command:
        if a.command not in subs:
            print("no such command: %s (try `harness.py help`)" % a.command, file=sys.stderr)
            return 2
        subs[a.command].print_help()
        for name, nested in sorted(_subcommands(subs[a.command]).items()):
            print("\n### harness.py %s %s" % (a.command, name))
            print(nested.format_help().strip())
        return 0
    print(__doc__.strip())
    for name in sorted(subs):
        print("\n" + "-" * 78)
        print("### harness.py %s" % name)
        print(subs[name].format_help().strip())
        for nested_name, nested in sorted(_subcommands(subs[name]).items()):
            print("\n  ### harness.py %s %s" % (name, nested_name))
            print(nested.format_help().strip())
    return 0


def build_parser():
    ap = argparse.ArgumentParser(
        prog="harness.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = ap.add_subparsers(dest="group", required=True)

    p = sub.add_parser("help", help="every command's help in one call, or one command's")
    p.add_argument("command", nargs="?")
    p.set_defaults(fn=cmd_help)

    p = sub.add_parser("list", help="every tool, grouped by what it does")
    p.add_argument("group", nargs="?", help="one group's tools, with the full hint for each")
    p.add_argument("--all", action="store_true", help="every file under tools/, by directory")
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser(
        "ci",
        help="hand off to tools/ci_tooling/ci.py: gen, check, baseline, cov, fmt, sonar",
        description="Everything after `ci` is passed to tools/ci_tooling/ci.py unread, so its own "
        "surface is the authority: `harness.py ci help`. The two that matter most: "
        "`ci gen --check` is what CI gates a generated region on, and `ci baseline <guard>` "
        "records a ratcheted guard's current violations as its new floor - a deliberate act, "
        "which is why it is its own command rather than a flag.",
        add_help=False,
    )
    p.add_argument("rest", nargs=argparse.REMAINDER, metavar="...")
    p.set_defaults(fn=cmd_ci)

    for name, table, blurb in GROUPS:
        p = sub.add_parser(
            name,
            help=blurb,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            epilog=table_help(table),
            add_help=True,
        )
        p.add_argument("name", choices=list(table))
        p.add_argument("rest", nargs=argparse.REMAINDER, metavar="...")
        p.set_defaults(fn=group_runner(table))

    p = sub.add_parser(
        "selftest",
        help="run the tools' own self-tests",
        description="Each case in these is a defect the tool shipped once - a module brief cut "
        "mid-clause, a hoist that crossed a #if, a dotted key that met its own output on the next "
        "pass. Run them after touching a converter, before running it over the tree.",
    )
    p.add_argument("names", nargs="*", metavar="NAME", help=" ".join(SELFTESTS))
    p.set_defaults(fn=cmd_selftest)

    doc = sub.add_parser("doc", help="tools/TOOLS.md generated tables").add_subparsers(dest="cmd", required=True)
    p = doc.add_parser(
        "gen",
        help="regenerate the derived tables in tools/TOOLS.md",
        description="The tables come from each file's flag surface, the write primitives it "
        "contains and the external commands it invokes - from the code, not from a docstring. "
        "Run it after adding a tool or changing a flag.",
    )
    p.add_argument("--check", action="store_true", help="exit 1 if the tables are stale (no write)")
    p.set_defaults(fn=cmd_doc_gen)
    return ap


def main():
    a = build_parser().parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
