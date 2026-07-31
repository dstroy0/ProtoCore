# PreemptQueue - real-time ingest with a preempting task queue

**Layer:** Foundation · **Build flags:** `PC_ENABLE_PREEMPT_QUEUE`

## What this example teaches

Hardware events (a DMA-complete interrupt, a GPIO edge, a bus byte) arrive in ISR
context, where you must do as little as possible. The **preempting work queue**
is the clean hand-off: the ISR posts a small fixed-size item and the scheduler
**preempts straight to a dedicated high-priority task** that does the real work -
so the heavy processing runs off the interrupt, immediately, not on the next loop
tick.

It is one queue feeding one core-pinned task, with static (zero-heap) storage:

```cpp
pc_pq_config cfg = {};
cfg.handler  = on_reading; // runs in the high-priority task, once per item
cfg.priority = 6;          // above loop(): a post preempts into the handler
cfg.core     = 1;          // pin the task
pc_pq_start(&cfg);
```

**Post from an ISR** - interrupt-safe, asks for an immediate context switch:

```cpp
void IRAM_ATTR sample_isr() {
    Reading r{ esp_timer_get_time(), g_seq++ };
    pc_pq_post_from_isr(&r);   // xQueueSendFromISR + portYIELD_FROM_ISR
}
```

**Post from a task** - back, urgent (front), each with a wait timeout, all
fail-closed (they return `false` and drop rather than block forever) when the
queue is full:

```cpp
pc_pq_post(&item, 0);          // to the back
pc_pq_post_urgent(&item, 0);   // jump the queue
```

On hardware the high-priority task drains the queue automatically. Measured on a
DevKitV1: an ISR post reaches the handler in ~12 us, and the queue never backs up
past one item (each post is processed before the next arrives).

**Sizing.** `PC_PQ_DEPTH` (items), `PC_PQ_ITEM_SIZE` (bytes per item), and
`PC_PQ_STACK` (task stack) are compile-time because the storage is static;
`pc_pq_high_water()` reports the peak depth so you can size `PC_PQ_DEPTH`.

**Build-flag note.** `PC_ENABLE_PREEMPT_QUEUE` must reach the library build (an
in-sketch `#define` does not reach the separately compiled library), so pass it as
a build flag:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_PREEMPT_QUEUE=1" \
  --lib="." examples/Foundation/PreemptQueue/PreemptQueue.ino
```
