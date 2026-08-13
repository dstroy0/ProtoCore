# DmaIngest - DMA peripheral ingest with a preempting task queue

**Layer:** Foundation · **Build flags:** `PROTOCORE_ENABLE_DMA`,
`PROTOCORE_ENABLE_PREEMPT_QUEUE`, `PROTOCORE_DMA_SIMULATE`

## What this example teaches

The high-throughput ingest path for field-bus peripherals (RS-485 UART, CAN over
SPI, IO-Link). A **DMA channel** moves peripheral bytes into a static buffer while
the CPU is free; when the transfer completes, a callback - **ISR context on real
silicon** - does the smallest possible thing: it posts the completion onto the
internal **DMA lane** of the **preempting work queue**
([PreemptQueue](../../Foundation/PreemptQueue/README.md)). That lane runs above the user lane,
so a dedicated high-priority task is preempted to process the bytes off the interrupt,
ahead of user work.

```
peripheral --DMA--> ping-pong buffer --complete--> callback (ISR)
                                                       |
                        protocore_pq_post_lane_from_isr(PROTOCORE_PQ_LANE_DMA, &msg)
                                                       |
                                    DMA-lane task (high prio) -> your handler
```

RX is **double-buffered (ping-pong)**: the completed buffer is handed up while the
DMA engine fills the other, so there is a full transfer of headroom to consume it.

## No loopback wire? Simulate the peripheral

There is usually no physical TX->RX jumper on the bench, so the channel runs the
built-in **ingress/egress simulator** (`PROTOCORE_DMA_SIMULATE=1`, the default):

- `protocore_dma_sim_feed()` injects bytes as if they arrived on the RX line,
- `protocore_dma_sim_capture()` reads back what egress DMA transmitted,
- a channel opened with `loopback = true` feeds its own egress into its ingress (an
  internal jumper),
- `protocore_dma_poll()` advances the engine and fires the completions.

This sketch opens a **loopback** channel and, each second, submits a 4-byte frame
for egress DMA; the jumper feeds it back into RX, the completion posts to the queue,
and the processing task prints it - the whole pipeline, end to end, **on the device
itself** with no wiring. Expected serial output:

```
dma ingest: egress DMA -> loopback -> RX complete -> preempt queue
RX ch0 seq1 4 bytes: DE AD 00 B0
RX ch0 seq3 4 bytes: DE AD 01 B1
RX ch0 seq5 4 bytes: DE AD 02 B2
```

(The odd sequence numbers are the RX completions; the even ones are the paired
TX-complete events on the same channel.)

## Real silicon

Set `PROTOCORE_DMA_SIMULATE=0` and provide a real driver behind the `protocore_dma_hw_*`
hooks (a UART UHCI / `spi_master` DMA backend). The sketch above is unchanged - it
only ever talks to `protocore_dma_open` / `protocore_dma_tx_submit` / the completion callback.

## Sizing

`PROTOCORE_DMA_CHANNELS` (channels) and `PROTOCORE_DMA_BUF_SIZE` (bytes per transfer, RX
double-buffered at this size) are compile-time because the storage is static.

## Build-flag note

The flags must reach the library build (an in-sketch `#define` does not reach the
separately compiled library), so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPROTOCORE_ENABLE_DMA=1 -DPROTOCORE_ENABLE_PREEMPT_QUEUE=1 -DPROTOCORE_DMA_SIMULATE=1" \
  --lib="." examples/Peripherals/DmaIngest/DmaIngest.ino
```
