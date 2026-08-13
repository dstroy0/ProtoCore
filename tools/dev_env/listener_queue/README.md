# listener_queue — Q7

**The question.** `TcpListenerNs::listener_queue` reports the event queue for a listener row, NULL
when the row is inactive. Does it stay unguarded, or live under
`#if PROTOCORE_WORKER_COUNT == 1`?

`worker_queue` is already guarded to `> 1`. If `listener_queue` takes the `== 1` guard the two are
mutually exclusive by construction, and `session.c` reaches whichever its build has.

**Why it is open.** `listener_add()` and `listener_add_dynamic()` create `lst->queue` with no worker
guard around them (`server.c:632`, `server.c:793`). `listener_enqueue()` reads `lst->queue` only in
its `#else` branch — the `PROTOCORE_WORKER_COUNT == 1` side. At `> 1` it sends to `lq.wq[owner]`
instead and never touches the listener's own queue. If that reading holds, the queue and its
`EVT_QUEUE_DEPTH * sizeof(TcpEvt)` backing store are reserved per listener and never sent to on
every multi-worker build.

**What the experiment establishes.**

1. _Static_ — every `protocore_platform_queue_send` site in `server.c`, tagged with the worker-count
   branch it sits under, plus every read of `lst->queue`. A send under `N>1` or outside a guard
   means the call must stay unguarded and the premise is wrong.
2. _Sizeof_ — `probe.c` includes the real `evt.h` and `protocore_config.h` and reports
   `MAX_LISTENERS`, `EVT_QUEUE_DEPTH`, `sizeof(TcpEvt)`, and the resulting per-listener and
   pool-wide byte counts at `N = 1, 2, 4, 8`. The numbers come from the headers, not from a mirror
   of them.

**Run**

```
bash tools/dev_env/listener_queue/run.sh
```

from the repo root. It builds only `probe.c`, not the library.

**Reading the result.** If the only `lst->queue` send is under `N==1`, then `listener_queue` takes
the `== 1` guard, and the creation in `listener_add` / `listener_add_dynamic` belongs under the same
guard — otherwise every multi-worker build carries the pool-wide byte count the probe prints as dead
BSS. If a send appears under `N>1`, `listener_queue` stays unguarded.

**Not measured here.** Whether the drain itself should invert into transport as a callback-taking
`drain` was already rejected: session owns dispatch, and inverting it adds an indirect call per
event. This experiment only decides the guard.
