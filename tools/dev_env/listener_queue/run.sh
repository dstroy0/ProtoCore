#!/usr/bin/env bash
# ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Q7: is TcpListenerNs::listener_queue unguarded, or guarded to PROTOCORE_WORKER_COUNT == 1?
#
# Two measurements, no build of the library itself:
#   1. static  - how many sites read lst->queue, and which worker-count branch each sits in
#   2. sizeof  - what a listener row spends on that queue at each worker count
#
# Run from the repo root:  bash tools/dev_env/listener_queue/run.sh

set -u

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SRC="$ROOT/src"
SERVER="$SRC/network_drivers/transport/tcp/server/server.c"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "=============================================================="
echo " 1. static: who reads the per-listener queue"
echo "=============================================================="
echo
echo "-- creation and send sites, with the guard each sits under --"
echo "   (guard '-' means the site is in neither branch: it is in every build)"
awk '
  BEGIN { guard = "-" }
  /^#if[ \t]+PROTOCORE_WORKER_COUNT[ \t]*>[ \t]*1/  { guard = "N>1";  next }
  /^#if[ \t]+PROTOCORE_WORKER_COUNT[ \t]*==[ \t]*1/ { guard = "N==1"; next }
  /^#else/  { if (guard == "N>1") guard = "N==1"; else if (guard == "N==1") guard = "N>1"; next }
  /^#endif/ { guard = "-"; next }
  /lst->queue = protocore_platform_queue_create/ { printf "  create  %-6s line %-5d\n", guard, NR }
  /protocore_platform_queue_send/                { printf "  send    %-6s line %-5d\n", guard, NR }
' "$SERVER"
echo
echo "-- reads of lst->queue --"
grep -n 'lst->queue' "$SERVER" | sed 's/^/  /'
echo

echo "=============================================================="
echo " 2. sizeof: what the queue costs at each worker count"
echo "=============================================================="
for n in 1 2 4 8; do
    echo
    # protocore_config.h includes board_profile.h repo-root-relative, so $ROOT is on the include path.
    # The platform's queue and control-block types come from the host driver under core_setup/hal/host,
    # the same one the native envs build against.
    cc -std=c11 -I "$SRC" -I "$ROOT" -I "$ROOT/core_setup/hal/host" \
       -include "$ROOT/core_setup/hal/host/protocore_net_host.h" \
       -DPROTOCORE_WORKER_COUNT="$n" \
       "$(dirname "$0")/probe.c" -o "$OUT/probe_$n" 2> "$OUT/err_$n"
    if [ $? -ne 0 ]; then
        echo "  build failed at N=$n:"
        sed 's/^/    /' "$OUT/err_$n" | head -20
        continue
    fi
    "$OUT/probe_$n" | sed 's/^/  /'
done

echo
echo "=============================================================="
echo " what this decides"
echo "=============================================================="
echo "  If the send sites show the only lst->queue send under N==1, then at N>1 every"
echo "  listener row reserves its queue storage and is never sent to. In that case"
echo "  listener_queue belongs under #if PROTOCORE_WORKER_COUNT == 1, mutually exclusive"
echo "  with worker_queue, and the creation in listener_add belongs under the same guard."
echo "  If a send site appears under N>1 or outside any guard, the call must stay unguarded."
