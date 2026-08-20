"""The hwcap refusal still refuses the edit it exists to refuse.

Loosening a guard is the easy half; proving it still fires is the half that matters. The change
teaches hwcap_orphaned what the env's base already carries, so the four cases are: a bare removal
with no base (still refused), the same removal where the base supplies the arm (allowed), a swap
(allowed, as before), and a removal of something that is not a capability arm at all (allowed).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.getcwd(), "test"))
import importlib.util

spec = importlib.util.spec_from_file_location("pc_harness", os.path.join(os.getcwd(), "test", "harness.py"))
H = importlib.util.module_from_spec(spec)
spec.loader.exec_module(H)

FAIL = 0


def check(name, cond):
    global FAIL
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAIL += 1


HW = "+<test/core_setup/hal/host/protocore_dma_host.c>"
SW = "+<test/core_setup/hal/portable/protocore_dma_portable.c>"
OTHER = "+<src/mmgr/arena/arena.c>"

before = {"src": [HW, OTHER]}
after_bare = {"src": [OTHER]}
after_swap = {"src": [SW, OTHER]}
after_other = {"src": [HW]}

check("a bare removal with no base is refused", bool(H.hwcap_orphaned(before, after_bare)))
check("the same removal is allowed when the base carries it", not H.hwcap_orphaned(before, after_bare, {HW}))
check("a base carrying a DIFFERENT arm does not excuse it", bool(H.hwcap_orphaned(before, after_bare, {SW})))
check("a swap is allowed, base or not", not H.hwcap_orphaned(before, after_swap))
check("dropping a non-arm source is not an orphaning", not H.hwcap_orphaned(before, after_other))

envs = {
    "base_env": {"src": [HW, OTHER]},
    "mid": {"base": "env:base_env", "src": []},
    "leaf": {"base": "env:mid", "src": [HW]},
    "loop_a": {"base": "env:loop_b", "src": []},
    "loop_b": {"base": "env:loop_a", "src": []},
}
check("the base chain is followed through a middle env", HW in H.inherited_src(envs, envs["leaf"]))
check("an env with no base inherits nothing", H.inherited_src(envs, envs["base_env"]) == set())
check("a base cycle terminates", isinstance(H.inherited_src(envs, envs["loop_a"]), set))

print("\nFAILURES: %d" % FAIL)
sys.exit(1 if FAIL else 0)
