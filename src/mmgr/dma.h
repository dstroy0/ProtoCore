// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dma.h
 * @brief DMA peripheral ingest / egress (PC_ENABLE_DMA) - the v5 high-throughput
 *        hardware-ingest path.
 *
 * A DMA channel moves bytes between a peripheral (UART / I2C / SPI) and a static
 * buffer while the CPU is free, then a DMA-complete event carries the result up.
 * Two directions:
 *
 *   - RX (ingress): peripheral -> DMA -> buffer. On completion the channel emits a
 *     @ref pc_dma_event whose `data`/`len` point at the just-filled buffer. RX is
 *     **double-buffered (ping-pong)**: the completed buffer is handed to the callback
 *     while the engine fills the other, so there is a full transfer of headroom to
 *     consume it before it is reused.
 *   - TX (egress): buffer -> DMA -> peripheral. The caller submits bytes; on
 *     completion the channel emits a TX event (data == nullptr) so the producer knows
 *     the buffer is free to refill.
 *
 * The completion callback runs in ISR context on real silicon, so keep it tiny - the
 * intended pattern is to post the event into the preempting work queue
 * (services/system/preempt_queue) with pc_pq_post_from_isr(), letting a high-priority task
 * do the real work off the interrupt. pc_dma stays decoupled from the queue: it just
 * hands you the event.
 *
 * **Simulator (PC_DMA_SIMULATE, default on).** With no physical loopback wire the
 * transfers run through an in-memory model of the peripheral: pc_dma_sim_feed() injects
 * bytes as if they arrived on the RX line, pc_dma_sim_capture() reads back what egress
 * DMA "transmitted", and a channel opened with `loopback` feeds its own TX egress into
 * its RX ingress (an internal jumper). pc_dma_poll() advances the engine and fires the
 * completions. This exercises the entire ingress -> event -> (queue) -> handler and
 * produce -> egress pipeline identically on the host bench and on-device. It is the
 * shipped, tested backend; a real silicon driver plugs into pc_dma_hw_* when the flag
 * is 0 (see docs/KNOWN_LIMITATIONS.md).
 *
 * Zero-heap: PC_DMA_CHANNELS channels, each with 2x PC_DMA_BUF_SIZE RX + one TX
 * buffer + the simulator's ingress/egress staging, all static and compile-time sized.
 * Fail-closed: a submit onto a busy channel or a feed past the staging capacity returns
 * false rather than blocking or overrunning.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DMA_H
#define PROTOCORE_DMA_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_DMA

/** @brief Peripheral a channel is bound to (informational; selects the real backend). */
typedef enum PROTO_ENUM_PACKED
{
    PC_DMA_UART = 0,
    PC_DMA_I2C = 1,
    PC_DMA_SPI = 2,
} pc_dma_periph;

/** @brief Transfer direction carried on a completion event. */
typedef enum PROTO_ENUM_PACKED
{
    PC_DMA_RX = 0, ///< ingress: peripheral -> buffer
    PC_DMA_TX = 1, ///< egress:  buffer -> peripheral
} pc_dma_dir;

/**
 * @brief A DMA-complete event handed to the channel callback.
 *
 * For RX, @ref data / @ref len point at the completed ping-pong buffer - valid only
 * until that buffer is reused (a transfer or two later). Consume it inside the callback,
 * or, if you hand the work to another task (e.g. post it to the preempting queue), copy
 * the @ref len bytes into the posted item: a deferred consumer can lag past the buffer's
 * reuse under load, so post the bytes, not this pointer. For TX, @ref data is nullptr and
 * @ref len is the number of bytes drained.
 */
typedef struct
{
    const uint8_t *data;  ///< RX: received bytes (into a ping-pong buffer); TX: nullptr
    uint32_t t_ms;        ///< pc_millis() at completion (0 on host builds)
    uint32_t t_us;        ///< pc_micros() at completion (0 on host builds) - a high-rate
                          ///< peripheral (SPI slave DMA off a fast external DAQ/scope) can
                          ///< complete several transfers inside one t_ms tick; use t_us to
                          ///< measure inter-transfer jitter / trigger latency at that rate.
    uint16_t len;         ///< bytes transferred
    uint16_t seq;         ///< per-channel completion sequence (wraps)
    uint8_t channel;      ///< channel id [0, PC_DMA_CHANNELS)
    pc_dma_periph periph; ///< pc_dma_periph
    pc_dma_dir dir;       ///< pc_dma_dir
    uint8_t _pad;
} pc_dma_event;

/**
 * @brief Called once per completed transfer (RX and TX). ISR context on real silicon,
 *        so keep it tiny (the canonical body posts @p ev to the preempting queue).
 * @param ev  the completion event (owned by the caller; copy what you need).
 * @param ctx the opaque pointer from @ref pc_dma_config.
 */
typedef void (*pc_dma_cb)(const pc_dma_event *ev, void *ctx);

/** @brief Channel configuration passed to pc_dma_open(). */
typedef struct
{
    uint8_t channel;       ///< channel id [0, PC_DMA_CHANNELS).
    pc_dma_periph periph;  ///< pc_dma_periph the channel drives.
    proto_bool loopback;   ///< simulator: this channel's TX egress feeds its own RX ingress.
    pc_dma_cb on_complete; ///< completion callback (required).
    void *ctx;             ///< opaque, forwarded to @ref on_complete.
} pc_dma_config;

/**
 * @brief Configure a channel and arm its RX transfer.
 * @return true if opened; false on a bad channel id / null callback / already open.
 */
proto_bool pc_dma_open(const pc_dma_config *cfg);

/**
 * @brief Submit bytes for egress DMA on channel @p ch (copies up to PC_DMA_BUF_SIZE).
 *
 * Fail-closed: returns false if a TX is still in flight on the channel (wait for its
 * TX-complete event), if the channel is closed, or on a null / oversize buffer.
 * @return true if the transfer was accepted.
 */
proto_bool pc_dma_tx_submit(uint8_t ch, const uint8_t *buf, uint16_t len);

/** @brief Close a channel and drop any in-flight transfer / staged simulator bytes. */
void pc_dma_close(uint8_t ch);

/**
 * @brief Advance the simulator engine: drain egress, run loopback, complete RX/TX and
 *        fire the callbacks. No-op on the real silicon backend (ISRs drive completion).
 *        This is how the host and the on-device self-test step the pipeline.
 */
void pc_dma_poll(void);

#if PC_DMA_SIMULATE

/**
 * @brief Simulator: inject @p len bytes as if they arrived on channel @p ch's RX line.
 *        Delivered as one or more RX events on the next pc_dma_poll().
 * @return true if staged; false if the ingress staging is full (fail-closed) or the
 *         channel is closed.
 */
proto_bool pc_dma_sim_feed(uint8_t ch, const uint8_t *bytes, uint16_t len);

/**
 * @brief Simulator: read back up to @p max bytes that channel @p ch transmitted via
 *        egress DMA. Consumes them from the capture buffer.
 * @return number of bytes copied into @p out.
 */
uint16_t pc_dma_sim_capture(uint8_t ch, uint8_t *out, uint16_t max);

#endif // PC_DMA_SIMULATE

#endif // PC_ENABLE_DMA

PROTO_END_DECLS

#endif // PROTOCORE_DMA_H
