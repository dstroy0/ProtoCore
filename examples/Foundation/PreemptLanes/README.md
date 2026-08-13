# PreemptLanes - internal preempting lanes vs the user lane

**Layer:** Foundation · **Build flags:** `PROTOCORE_ENABLE_PREEMPT_QUEUE`

## What this example teaches

The preempting work queue ([PreemptQueue](../PreemptQueue/README.md)) is not one
queue but several named **lanes**, each with its own task at its own priority:

| Lane                        | Role                                              | Priority |
| --------------------------- | ------------------------------------------------- | -------- |
| `PROTOCORE_PQ_LANE_DMA`     | internal: DMA peripheral transfers                | highest  |
| `PROTOCORE_PQ_LANE_FORWARD` | internal: interface forwarding                    | high     |
| `PROTOCORE_PQ_LANE_DEVICE`  | internal: device access                           | high     |
| `PROTOCORE_PQ_LANE_USER`    | **exposed to your app** (no-arg `protocore_pq_*`) | lowest   |

The internal lanes run **above** the user lane (base `PROTOCORE_PQ_INTERNAL_PRIORITY`, DMA
highest) and below the lwIP tcpip / WiFi tasks. So the library's own real-time ingest
(a DMA transfer, a forwarded frame, a device event) always preempts user "process now"
work, and neither starves networking.

```cpp
// internal lane (priority 0 -> the lane default, above the user lane):
protocore_pq_config dma = {}; dma.handler = on_critical; dma.core = 1;
protocore_pq_start_lane(protocore_pq_lane::PROTOCORE_PQ_LANE_DMA, &dma);
protocore_pq_post_lane(protocore_pq_lane::PROTOCORE_PQ_LANE_DMA, &item, 0);

// user lane - the no-arg API is unchanged (it drives PROTOCORE_PQ_LANE_USER):
protocore_pq_config user = {}; user.handler = on_background; user.priority = 5;
protocore_pq_start(&user);
protocore_pq_post(&item, 0);
```

`protocore_pq_lane_priority(lane)` returns a lane's default priority, so you can confirm the
ordering. Each lane is independent: separate queue, separate task, separate
`protocore_pq_high_water_lane()`. Queue storage is static (zero heap); a lane's task stack is
created only when you start it, so lanes you never start cost only their queue storage.

## Zero heap, fail-closed

Same guarantees as one lane: `PROTOCORE_PQ_DEPTH` x `PROTOCORE_PQ_ITEM_SIZE` static storage per
lane, fail-closed on a full queue, no hot-path locks.

## Build-flag note

`PROTOCORE_ENABLE_PREEMPT_QUEUE` must reach the library build, so pass it as a build flag:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPROTOCORE_ENABLE_PREEMPT_QUEUE=1" \
  --lib="." examples/Foundation/PreemptLanes/PreemptLanes.ino
```
