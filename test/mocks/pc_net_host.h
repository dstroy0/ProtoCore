// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host pcb driver: the target's scheduler + TCP/UDP surface, implemented for the test build.
//
// src/board_drivers/board_profiles/pc_platform.h aliases that surface onto the vendor's calls on
// the hot path. On the test build there is no vendor, so this file supplies the same names, the
// same shapes, and the same member layout the core reads. That is what lets a transport TU be
// compiled and driven on the host instead of only on silicon.
//
// It is deliberately inert: pcbs come from a fixed table, sends are captured, and callbacks are
// stored so a test can fire them. Nothing here talks to a socket. A test that wants behavior
// drives it through the pc_net_host_* entry points at the bottom.
//
// Every mutable global below carries external linkage through a weak definition rather than
// `static`. A `static` in a header is one object PER TRANSLATION UNIT, so the core wrote its own
// copy of the send capture while the test read a different one and found it empty - the state is
// only a seam if both sides reach the same bytes. Weak lets every TU emit the same definition and
// the linker keep exactly one.
//
// test/ is exempt from the src/ style rules, so this reads as plain host C.

#ifndef PROTOCORE_PC_NET_HOST_H
#define PROTOCORE_PC_NET_HOST_H

#include <Arduino.h> // the virtual clock the host time base reads: millis() / set_millis()
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Buses
// ---------------------------------------------------------------------------
//
// Loopback, not silence: what the core writes is captured and can be read back, and a test can
// preload what a read should return. That makes the bridge's framing and transaction logic
// exercisable on the host instead of only on a wired rig.

#define PC_UART_UNITS 3

#ifndef PC_BUS_HOST_CAP
#define PC_BUS_HOST_CAP 1024
#endif

__attribute__((weak)) uint8_t pc_bus_host_tx[PC_BUS_HOST_CAP];
__attribute__((weak)) uint32_t pc_bus_host_tx_len;
__attribute__((weak)) uint8_t pc_bus_host_rx[PC_BUS_HOST_CAP];
__attribute__((weak)) uint32_t pc_bus_host_rx_len;
__attribute__((weak)) uint32_t pc_bus_host_rx_pos;

static inline void pc_bus_host_capture(const void *buf, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)buf;
    while (len-- && pc_bus_host_tx_len < PC_BUS_HOST_CAP)
    {
        pc_bus_host_tx[pc_bus_host_tx_len++] = *b++;
    }
}
static inline uint32_t pc_bus_host_drain(uint8_t *buf, uint32_t len)
{
    uint32_t n = 0;
    while (n < len && pc_bus_host_rx_pos < pc_bus_host_rx_len)
    {
        buf[n++] = pc_bus_host_rx[pc_bus_host_rx_pos++];
    }
    return n;
}

static inline int pc_platform_uart_begin(uint8_t unit, uint32_t baud)
{
    (void)baud;
    return (unit < PC_UART_UNITS) ? 1 : 0;
}
static inline int pc_platform_uart_write(uint8_t unit, const void *buf, uint32_t len)
{
    (void)unit;
    pc_bus_host_capture(buf, len);
    return (int)len;
}
static inline int pc_platform_uart_read(uint8_t unit, void *buf, uint32_t len, uint32_t ms)
{
    (void)unit;
    (void)ms;
    return (int)pc_bus_host_drain((uint8_t *)buf, len);
}
static inline uint32_t pc_platform_uart_available(uint8_t unit)
{
    (void)unit;
    return pc_bus_host_rx_len - pc_bus_host_rx_pos;
}

#define PC_SPI_MSBFIRST 0
#define PC_SPI_LSBFIRST 1

__attribute__((weak)) int pc_spi_host_up;

static inline int pc_platform_spi_begin(int mosi, int miso, int sclk)
{
    (void)mosi;
    (void)miso;
    (void)sclk;
    pc_spi_host_up = 1;
    return 1;
}
static inline int pc_platform_spi_txn(uint32_t hz, uint8_t bit_order, uint8_t mode, const uint8_t *tx, uint8_t *rx,
                                      uint32_t len)
{
    (void)hz;
    (void)bit_order;
    (void)mode;
    if (!pc_spi_host_up)
    {
        return 0;
    }
    if (tx)
    {
        pc_bus_host_capture(tx, len);
    }
    if (rx)
    {
        uint32_t got = pc_bus_host_drain(rx, len);
        while (got < len)
        {
            rx[got++] = 0;
        }
    }
    return 1;
}

/** @brief Bytes the core has driven onto any bus since the last reset. */
static inline const uint8_t *pc_bus_host_written(uint32_t *len)
{
    if (len)
    {
        *len = pc_bus_host_tx_len;
    }
    return pc_bus_host_tx;
}
/** @brief Preload what the next bus reads return. */
static inline void pc_bus_host_preload(const uint8_t *data, uint32_t len)
{
    pc_bus_host_rx_len = (len > PC_BUS_HOST_CAP) ? PC_BUS_HOST_CAP : len;
    pc_bus_host_rx_pos = 0;
    for (uint32_t i = 0; i < pc_bus_host_rx_len; i++)
    {
        pc_bus_host_rx[i] = data[i];
    }
}
/** @brief Drop both directions so each test starts clean. */
static inline void pc_bus_host_reset(void)
{
    pc_bus_host_tx_len = 0;
    pc_bus_host_rx_len = 0;
    pc_bus_host_rx_pos = 0;
}

// ---------------------------------------------------------------------------
// Entropy
// ---------------------------------------------------------------------------
//
// NOT a CSPRNG and not a stand-in for one. On the target this call reaches a true hardware
// source (thermal / RF noise); here it is a deterministic xorshift so a failing crypto test
// reproduces exactly. Nothing built on this host build is secret, and no key produced here
// should ever leave a test.
//
// pc_rand_host_seed() makes a run repeatable from a chosen point.

__attribute__((weak)) uint32_t pc_rand_host_state = 0x2545F491u;

static inline uint32_t pc_platform_rand_u32(void)
{
    uint32_t x = pc_rand_host_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pc_rand_host_state = x;
    return x;
}
static inline void pc_platform_rand_fill(void *buf, size_t len)
{
    uint8_t *b = (uint8_t *)buf;
    while (len >= 4)
    {
        uint32_t v = pc_platform_rand_u32();
        b[0] = (uint8_t)v;
        b[1] = (uint8_t)(v >> 8);
        b[2] = (uint8_t)(v >> 16);
        b[3] = (uint8_t)(v >> 24);
        b += 4;
        len -= 4;
    }
    uint32_t v = pc_platform_rand_u32();
    while (len--)
    {
        *b++ = (uint8_t)v;
        v >>= 8;
    }
}

/** @brief Restart the host generator from @p seed so a run reproduces. */
static inline void pc_rand_host_seed(uint32_t seed)
{
    pc_rand_host_state = seed ? seed : 0x2545F491u;
}

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------
//
// A pin table rather than a no-op: a test sets an input level with pc_gpio_host_set() and reads
// back what the core drove with pc_gpio_host_level(), so pin logic is exercised on the host.

#define PC_GPIO_IN 0
#define PC_GPIO_OUT 1
#define PC_GPIO_IN_PULLUP 2
#define PC_GPIO_IN_PULLDOWN 3
#define PC_GPIO_LOW 0
#define PC_GPIO_HIGH 1

#ifndef PC_GPIO_HOST_PINS
#define PC_GPIO_HOST_PINS 64
#endif
__attribute__((weak)) uint8_t pc_gpio_host_mode_tbl[PC_GPIO_HOST_PINS];
__attribute__((weak)) uint8_t pc_gpio_host_level_tbl[PC_GPIO_HOST_PINS];

static inline void pc_platform_gpio_mode(uint8_t pin, uint8_t mode)
{
    if (pin < PC_GPIO_HOST_PINS)
    {
        pc_gpio_host_mode_tbl[pin] = mode;
        if (mode == PC_GPIO_IN_PULLUP)
        {
            pc_gpio_host_level_tbl[pin] = 1;
        }
        else if (mode == PC_GPIO_IN_PULLDOWN)
        {
            pc_gpio_host_level_tbl[pin] = 0;
        }
    }
}
static inline void pc_platform_gpio_write(uint8_t pin, uint8_t level)
{
    if (pin < PC_GPIO_HOST_PINS)
    {
        pc_gpio_host_level_tbl[pin] = level ? 1 : 0;
    }
}
static inline uint8_t pc_platform_gpio_read(uint8_t pin)
{
    return (pin < PC_GPIO_HOST_PINS) ? pc_gpio_host_level_tbl[pin] : 0;
}

/** @brief Drive an input pin from a test. */
static inline void pc_gpio_host_set(uint8_t pin, uint8_t level)
{
    pc_platform_gpio_write(pin, level);
}
/** @brief What the core last drove (or a test last set) on @p pin. */
static inline uint8_t pc_gpio_host_level(uint8_t pin)
{
    return pc_platform_gpio_read(pin);
}
/** @brief The direction the core configured for @p pin. */
static inline uint8_t pc_gpio_host_mode(uint8_t pin)
{
    return (pin < PC_GPIO_HOST_PINS) ? pc_gpio_host_mode_tbl[pin] : 0;
}

// ---------------------------------------------------------------------------
// Time base
// ---------------------------------------------------------------------------
//
// The host has no tick timer, so the platform default is the virtual clock in the Arduino mock:
// set_millis() moves it and nothing else does. That makes the default path deterministic, which
// is what a timeout test drives when it reverts an override with pc_set_clock(NULL, 0).

static inline uint32_t pc_platform_millis(void)
{
    return millis();
}
static inline uint32_t pc_platform_micros(void)
{
    return millis() * 1000u;
}
// No cycle counter on the host; deltas only, so a micros-derived stand-in is honest enough.
static inline uint32_t pc_platform_cycles(void)
{
    return pc_platform_micros() * 240u;
}

// ---------------------------------------------------------------------------
// Scheduler surface
// ---------------------------------------------------------------------------

typedef void *pc_platform_queue;
typedef struct
{
    uint8_t opaque[8];
} pc_platform_queue_ctrl;
typedef void *pc_platform_task;
typedef void (*pc_platform_task_fn)(void *);
typedef int pc_platform_status;
typedef uint32_t pc_platform_ticks;

#define PC_PLATFORM_OK 1
#define PC_PLATFORM_PASS 1
#define PC_PLATFORM_FALSE 0
#define PC_PLATFORM_WAIT_FOREVER 0xFFFFFFFFu
#define PC_PLATFORM_CORES 1

// The host runs the pipeline inline, so a queue is a handle the core can hold and compare and a
// task never starts. Depth/'item size' are accepted and ignored.
// One-shot creation failure: the next queue create reports no room, the way a kernel out of
// queue objects does, so the caller has to unwind the listener it was building.
__attribute__((weak)) int pc_platform_queue_create_fail_once;

static inline void mock_queue_create_fail_once(void)
{
    pc_platform_queue_create_fail_once = 1;
}

static inline pc_platform_queue pc_platform_queue_create(size_t depth, size_t item, void *storage, void *ctrl)
{
    (void)depth;
    (void)item;
    (void)ctrl;
    if (pc_platform_queue_create_fail_once)
    {
        pc_platform_queue_create_fail_once = 0;
        return NULL;
    }
    return storage ? storage : (void *)1;
}
// One-shot send failure: the next pc_platform_queue_send() reports a full queue and clears the
// latch. Lets a test drive the enqueue path's rejection branch.
__attribute__((weak)) int pc_platform_queue_send_fail_once = 0;

static inline void mock_queue_send_fail_once(void)
{
    pc_platform_queue_send_fail_once = 1;
}

static inline int pc_platform_queue_send(pc_platform_queue q, const void *item, uint32_t ticks)
{
    (void)q;
    (void)item;
    (void)ticks;
    if (pc_platform_queue_send_fail_once)
    {
        pc_platform_queue_send_fail_once = 0;
        return PC_PLATFORM_FALSE;
    }
    return PC_PLATFORM_OK;
}
static inline int pc_platform_queue_send_front(pc_platform_queue q, const void *item, uint32_t ticks)
{
    (void)q;
    (void)item;
    (void)ticks;
    return PC_PLATFORM_OK;
}
static inline int pc_platform_queue_send_isr(pc_platform_queue q, const void *item, int *woke)
{
    (void)q;
    (void)item;
    if (woke)
    {
        *woke = 0;
    }
    return PC_PLATFORM_OK;
}
// Staged-event buffer: a test calls queue_stage_raw() before server_tick(), and the receive below
// drains those items FIFO and then reports empty, which is what a real queue does once emptied.
// A send is still inert (the host runs the pipeline inline), so the only way an item enters is a
// test staging it deliberately. One instance for the whole program - the test stages from its own
// translation unit and the session layer drains from another - so the definition is weak and the
// linker collapses it, the same way the millis counter in Arduino.h is shared.
#define PC_QUEUE_STAGE_MAX 16
#define PC_QUEUE_STAGE_ITEM 32

typedef struct
{
    uint8_t items[PC_QUEUE_STAGE_MAX][PC_QUEUE_STAGE_ITEM];
    int item_sz[PC_QUEUE_STAGE_MAX];
    int count;
    int idx;
} PcQueueStage;
__attribute__((weak)) PcQueueStage g_pc_queue_stage;

static inline void queue_stage_raw(const void *item, int sz)
{
    if (sz > 0 && sz <= PC_QUEUE_STAGE_ITEM && g_pc_queue_stage.count < PC_QUEUE_STAGE_MAX)
    {
        memcpy(g_pc_queue_stage.items[g_pc_queue_stage.count], item, (size_t)sz);
        g_pc_queue_stage.item_sz[g_pc_queue_stage.count] = sz;
        g_pc_queue_stage.count++;
    }
}

static inline void queue_stage_reset(void)
{
    g_pc_queue_stage.count = 0;
    g_pc_queue_stage.idx = 0;
}

static inline int pc_platform_queue_recv(pc_platform_queue q, void *item, uint32_t ticks)
{
    (void)q;
    (void)ticks;
    if (g_pc_queue_stage.idx < g_pc_queue_stage.count)
    {
        memcpy(item, g_pc_queue_stage.items[g_pc_queue_stage.idx],
               (size_t)g_pc_queue_stage.item_sz[g_pc_queue_stage.idx]);
        g_pc_queue_stage.idx++;
        return PC_PLATFORM_OK;
    }
    return 0;
}
static inline size_t pc_platform_queue_waiting(pc_platform_queue q)
{
    (void)q;
    return 0;
}
static inline size_t pc_platform_queue_waiting_isr(pc_platform_queue q)
{
    (void)q;
    return 0;
}
static inline void pc_platform_queue_delete(pc_platform_queue q)
{
    (void)q;
}

static inline int pc_platform_task_start(pc_platform_task_fn fn, const char *name, uint32_t stack, void *arg, int prio,
                                         pc_platform_task *out, int core)
{
    (void)fn;
    (void)name;
    (void)stack;
    (void)arg;
    (void)prio;
    (void)core;
    if (out)
    {
        *out = (void *)1;
    }
    return PC_PLATFORM_PASS;
}
static inline void pc_platform_task_stop(pc_platform_task t)
{
    (void)t;
}
static inline void pc_platform_task_notify(pc_platform_task t)
{
    (void)t;
}
static inline uint32_t pc_platform_task_wait(int clear, uint32_t ticks)
{
    (void)clear;
    (void)ticks;
    return 0;
}
static inline void pc_platform_task_delay(uint32_t ticks)
{
    (void)ticks;
}
static inline void pc_platform_task_yield_from_isr(int woke)
{
    (void)woke;
}
static inline pc_platform_task pc_platform_task_self(void)
{
    return (void *)1;
}

// ---------------------------------------------------------------------------
// Address
// ---------------------------------------------------------------------------

#define PC_NET_TYPE_ANY 0
#define PC_NET_TYPE_V4 4
#define PC_NET_TYPE_V6 6

typedef struct
{
    uint8_t type;      // PC_NET_TYPE_*
    uint8_t bytes[16]; // network order; v4 in the first 4
} pc_net_ip;

// Behind a function rather than a file-scope object: this header reaches every translation unit,
// and a static variable most of them never name warns in each one.
static inline pc_net_ip *pc_net_host_any(void)
{
    static pc_net_ip any;
    return &any;
}
#define PC_NET_ADDR_ANY pc_net_host_any()
#define PC_NET_ADDR_ANY4 pc_net_host_any()
#define PC_NET_ADDR_ANY4_P pc_net_host_any()

#define pc_net_ip_is_v4(a) ((a) && (a)->type == PC_NET_TYPE_V4)
#define pc_net_ip_is_v6(a) ((a) && (a)->type == PC_NET_TYPE_V6)
#define pc_net_ip_as_v4(a) (a)
#define pc_net_ip_as_v6(a) (a)

static inline uint32_t pc_net_ip4_u32(const pc_net_ip *a)
{
    if (!a)
    {
        return 0;
    }
    return ((uint32_t)a->bytes[0] << 24) | ((uint32_t)a->bytes[1] << 16) | ((uint32_t)a->bytes[2] << 8) |
           (uint32_t)a->bytes[3];
}
static inline void pc_net_ip4_set(pc_net_ip *a, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    if (!a)
    {
        return;
    }
    memset(a, 0, sizeof(*a));
    a->type = PC_NET_TYPE_V4;
    a->bytes[0] = b0;
    a->bytes[1] = b1;
    a->bytes[2] = b2;
    a->bytes[3] = b3;
}
static inline int pc_net_ip4_is_multicast(const pc_net_ip *a)
{
    return a && a->type == PC_NET_TYPE_V4 && (a->bytes[0] & 0xF0u) == 0xE0u;
}
// Dotted-quad only; the core's own RFC 4291 parser (network/ip.c) is what tests actually exercise.
static inline int pc_net_ip_parse(const char *s, pc_net_ip *out)
{
    if (!s || !out)
    {
        return 0;
    }
    unsigned v[4] = {0, 0, 0, 0};
    int n = 0, cur = 0, digits = 0;
    for (const char *p = s;; p++)
    {
        if (*p >= '0' && *p <= '9')
        {
            cur = cur * 10 + (*p - '0');
            digits++;
        }
        else if (*p == '.' || *p == '\0')
        {
            if (!digits || cur > 255 || n > 3)
            {
                return 0;
            }
            v[n++] = (unsigned)cur;
            cur = 0;
            digits = 0;
            if (*p == '\0')
            {
                break;
            }
        }
        else
        {
            return 0;
        }
    }
    if (n != 4)
    {
        return 0;
    }
    pc_net_ip4_set(out, (uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]);
    return 1;
}
static inline char *pc_net_ip_print(const pc_net_ip *a, char *buf, int cap)
{
    if (!buf || cap <= 0)
    {
        return buf;
    }
    if (!a)
    {
        buf[0] = '\0';
        return buf;
    }
    int w = 0;
    for (int i = 0; i < 4 && w < cap - 1; i++)
    {
        unsigned b = a->bytes[i];
        if (b >= 100 && w < cap - 1)
        {
            buf[w++] = (char)('0' + b / 100);
        }
        if (b >= 10 && w < cap - 1)
        {
            buf[w++] = (char)('0' + (b / 10) % 10);
        }
        if (w < cap - 1)
        {
            buf[w++] = (char)('0' + b % 10);
        }
        if (i != 3 && w < cap - 1)
        {
            buf[w++] = '.';
        }
    }
    buf[w] = '\0';
    return buf;
}

// ---------------------------------------------------------------------------
// Packet buffer
// ---------------------------------------------------------------------------

#define PC_NET_PBUF_TRANSPORT 0
#define PC_NET_PBUF_RAM 0

typedef struct pc_pbuf
{
    struct pc_pbuf *next;
    void *payload;
    uint16_t tot_len;
    uint16_t len;
} pc_pbuf;

// ---------------------------------------------------------------------------
// Result codes
// ---------------------------------------------------------------------------

typedef int8_t pc_net_err;

#define PC_NET_OK 0
#define PC_NET_ERR_MEM (-1)
#define PC_NET_ERR_BUF (-2)
#define PC_NET_ERR_VAL (-6)
#define PC_NET_ERR_ARG (-16)
#define PC_NET_ERR_USE (-8)
#define PC_NET_ERR_CONN (-11)
#define PC_NET_ERR_CLSD (-15)
#define PC_NET_ERR_RST (-14)
#define PC_NET_ERR_ABRT (-13)

#define PC_NET_WRITE_COPY 0x01
#define PC_NET_OPT_REUSEADDR 0x04

// ---------------------------------------------------------------------------
// Control blocks
// ---------------------------------------------------------------------------

typedef struct pc_pcb pc_pcb;
typedef struct pc_udp_pcb pc_udp_pcb;

typedef pc_net_err (*pc_net_recv_fn)(void *, pc_pcb *, pc_pbuf *, pc_net_err);
typedef pc_net_err (*pc_net_sent_fn)(void *, pc_pcb *, uint16_t);
typedef pc_net_err (*pc_net_accept_fn)(void *, pc_pcb *, pc_net_err);
typedef pc_net_err (*pc_net_connect_fn)(void *, pc_pcb *, pc_net_err);
typedef void (*pc_net_err_fn)(void *, pc_net_err);
typedef void (*pc_net_udp_recv_fn)(void *, pc_udp_pcb *, pc_pbuf *, const pc_net_ip *, uint16_t);

struct pc_pcb
{
    uint8_t tos;
    uint8_t state;
    uint16_t local_port;
    uint16_t remote_port;
    pc_net_ip local_ip;
    pc_net_ip remote_ip;
    uint32_t so_options;
    uint16_t snd_queuelen; // segments still unacknowledged; the core waits for this to drain
    void *arg;
    pc_net_recv_fn on_recv;
    pc_net_sent_fn on_sent;
    pc_net_accept_fn on_accept;
    pc_net_err_fn on_err;
    int in_use;
};

struct pc_udp_pcb
{
    uint8_t tos;
    uint16_t local_port;
    void *arg;
    pc_net_udp_recv_fn on_recv;
    uint32_t so_options;
    int in_use;
};

// Fixed pools: no allocation, and a test can walk them to see what the core opened.
#ifndef PC_NET_HOST_PCBS
#define PC_NET_HOST_PCBS 16
#endif
__attribute__((weak)) pc_pcb pc_net_host_pcbs[PC_NET_HOST_PCBS];
__attribute__((weak)) pc_udp_pcb pc_net_host_udp_pcbs[PC_NET_HOST_PCBS];

/**
 * @brief A stable pcb a test can bind a slot to, when what it needs is only "this slot has one".
 *
 * Slot state that is set up by hand rather than by an accept still has to carry a non-null pcb,
 * because the core reads it to decide a connection is live. The last entry is reserved for that:
 * pc_net_new() hands out from the front, so a test holding this one never collides with a pcb the
 * code under test allocated.
 */
static inline pc_pcb *pc_net_host_pcb(void)
{
    return &pc_net_host_pcbs[PC_NET_HOST_PCBS - 1];
}

// One-shot allocation failure: the next pc_net_new() reports the control-block pool spent.
__attribute__((weak)) int pc_net_host_new_fail_once;

static inline void mock_new_pcb_fail_once(void)
{
    pc_net_host_new_fail_once = 1;
}

static inline pc_pcb *pc_net_new(int type)
{
    (void)type;
    if (pc_net_host_new_fail_once)
    {
        pc_net_host_new_fail_once = 0;
        return NULL;
    }
    for (int i = 0; i < PC_NET_HOST_PCBS; i++)
    {
        if (!pc_net_host_pcbs[i].in_use)
        {
            memset(&pc_net_host_pcbs[i], 0, sizeof(pc_pcb));
            pc_net_host_pcbs[i].in_use = 1;
            return &pc_net_host_pcbs[i];
        }
    }
    return NULL;
}
// One-shot bind failure: the next bind reports the address already in use.
__attribute__((weak)) int pc_net_host_bind_fail_once;

static inline void mock_bind_fail_once(void)
{
    pc_net_host_bind_fail_once = 1;
}

static inline pc_net_err pc_net_bind(pc_pcb *p, const pc_net_ip *a, uint16_t port)
{
    (void)a;
    if (pc_net_host_bind_fail_once)
    {
        pc_net_host_bind_fail_once = 0;
        return PC_NET_ERR_USE;
    }
    if (!p)
    {
        return PC_NET_ERR_ARG;
    }
    p->local_port = port;
    return PC_NET_OK;
}
// One-shot listen failure: the next listen reports no memory for the listen block.
__attribute__((weak)) int pc_net_host_listen_fail_once;

static inline void mock_listen_fail_once(void)
{
    pc_net_host_listen_fail_once = 1;
}

static inline pc_pcb *pc_net_listen(pc_pcb *p, uint8_t backlog)
{
    (void)backlog;
    if (pc_net_host_listen_fail_once)
    {
        pc_net_host_listen_fail_once = 0;
        return NULL;
    }
    return p;
}
static inline pc_net_err pc_net_connect(pc_pcb *p, const pc_net_ip *a, uint16_t port, pc_net_connect_fn cb)
{
    (void)cb;
    if (!p)
    {
        return PC_NET_ERR_ARG;
    }
    if (a)
    {
        p->remote_ip = *a;
    }
    p->remote_port = port;
    return PC_NET_OK;
}
// One-shot close failure: the next pc_net_close() reports no memory and leaves the pcb open, the
// way a stack that cannot queue the FIN does. The caller has to keep the slot draining, not drop it.
__attribute__((weak)) int pc_net_host_close_fail_once;

static inline void mock_close_fail_once(void)
{
    pc_net_host_close_fail_once = 1;
}

static inline pc_net_err pc_net_close(pc_pcb *p)
{
    if (pc_net_host_close_fail_once)
    {
        pc_net_host_close_fail_once = 0;
        return PC_NET_ERR_MEM;
    }
    if (p)
    {
        p->in_use = 0;
    }
    return PC_NET_OK;
}
// How many aborts the code under test has issued. A slot reaped by an accept gate or a timeout
// sweep is only distinguishable from one closed cleanly by whether the stack was told to abort.
__attribute__((weak)) int pc_net_host_abort_calls;

static inline int mock_abort_call_count(void)
{
    return pc_net_host_abort_calls;
}

static inline void mock_abort_call_reset(void)
{
    pc_net_host_abort_calls = 0;
}

static inline void pc_net_abort(pc_pcb *p)
{
    pc_net_host_abort_calls++;
    if (p)
    {
        p->in_use = 0;
    }
}
static inline void pc_net_arg(pc_pcb *p, void *arg)
{
    if (p)
    {
        p->arg = arg;
    }
}
static inline void pc_net_on_recv(pc_pcb *p, pc_net_recv_fn fn)
{
    if (p)
    {
        p->on_recv = fn;
    }
}
static inline void pc_net_on_sent(pc_pcb *p, pc_net_sent_fn fn)
{
    if (p)
    {
        p->on_sent = fn;
    }
}
static inline void pc_net_on_accept(pc_pcb *p, pc_net_accept_fn fn)
{
    if (p)
    {
        p->on_accept = fn;
    }
}
static inline void pc_net_on_err(pc_pcb *p, pc_net_err_fn fn)
{
    if (p)
    {
        p->on_err = fn;
    }
}

// Sends are captured, not transmitted; pc_net_host_sent() is what a test asserts on.
// Large enough to hold a multi-window file response whole: a test asserts on the body it sent, and
// a capture that stops at one window would report a truncation the transport never made.
#ifndef PC_NET_HOST_TXCAP
#define PC_NET_HOST_TXCAP 65536
#endif
__attribute__((weak)) uint8_t pc_net_host_tx[PC_NET_HOST_TXCAP];
__attribute__((weak)) size_t pc_net_host_tx_len;

// After this many successful writes the next pc_net_write reports a full send buffer and queues
// nothing, so a send pump takes its un-read-and-retry path. -1 never fails.
__attribute__((weak)) int pc_net_host_write_fail_after = -1;

static inline void mock_send_fail_after(int n)
{
    pc_net_host_write_fail_after = n;
}

static inline pc_net_err pc_net_write(pc_pcb *p, const void *data, uint16_t len, uint8_t flags)
{
    (void)p;
    (void)flags;
    if (!data)
    {
        return PC_NET_ERR_ARG;
    }
    if (pc_net_host_write_fail_after == 0)
    {
        return PC_NET_ERR_MEM;
    }
    if (pc_net_host_write_fail_after > 0)
    {
        pc_net_host_write_fail_after--;
    }
    if (pc_net_host_tx_len + len > sizeof(pc_net_host_tx))
    {
        return PC_NET_ERR_MEM;
    }
    memcpy(pc_net_host_tx + pc_net_host_tx_len, data, len);
    pc_net_host_tx_len += len;
    return PC_NET_OK;
}
static inline pc_net_err pc_net_output(pc_pcb *p)
{
    (void)p;
    return PC_NET_OK;
}
static inline void pc_net_recved(pc_pcb *p, uint16_t len)
{
    (void)p;
    (void)len;
}
// How much room the stack reports for the next write. A test shrinks it to drive the
// backpressure-and-resume path, where a response has to page out across several worker loops
// instead of fitting one send.
#define MOCK_SNDBUF_DEFAULT 5744 /* a typical lwIP TCP_SND_BUF */
__attribute__((weak)) uint16_t pc_net_host_sndbuf_val = MOCK_SNDBUF_DEFAULT;

static inline void mock_sndbuf_set(uint16_t v)
{
    pc_net_host_sndbuf_val = v;
}

static inline uint16_t pc_net_sndbuf(pc_pcb *p)
{
    (void)p;
    return pc_net_host_sndbuf_val;
}
static inline void pc_net_nagle_disable(pc_pcb *p)
{
    (void)p;
}
static inline void pc_net_rcv_wnd_update(pc_pcb *p, uint16_t len)
{
    (void)p;
    (void)len;
}
static inline void pc_net_opt_set(void *p, uint32_t opt)
{
    (void)p;
    (void)opt;
}

static inline void pc_net_pbuf_free(pc_pbuf *p)
{
    (void)p;
}
static inline uint16_t pc_net_pbuf_copy(const pc_pbuf *p, void *dst, uint16_t len, uint16_t off)
{
    if (!p || !dst || !p->payload)
    {
        return 0;
    }
    uint16_t avail = (uint16_t)(p->len > off ? p->len - off : 0);
    uint16_t n = len < avail ? len : avail;
    memcpy(dst, (const uint8_t *)p->payload + off, n);
    return n;
}
static inline pc_pbuf *pc_net_pbuf_alloc(int layer, uint16_t len, int type)
{
    (void)layer;
    (void)type;
    (void)len;
    return NULL; // a test that needs a pbuf builds one and calls the callback directly
}

// First member of the core's call record; fn casts back to that record. lwIP puts a semaphore here.
typedef struct pc_net_call
{
    int sem;
} pc_net_call;

static inline pc_net_err pc_net_call_marshal(pc_net_err (*fn)(pc_net_call *), pc_net_call *c)
{
    return fn ? fn(c) : PC_NET_OK;
}

static inline pc_udp_pcb *pc_net_udp_new(void)
{
    for (int i = 0; i < PC_NET_HOST_PCBS; i++)
    {
        if (!pc_net_host_udp_pcbs[i].in_use)
        {
            memset(&pc_net_host_udp_pcbs[i], 0, sizeof(pc_udp_pcb));
            pc_net_host_udp_pcbs[i].in_use = 1;
            return &pc_net_host_udp_pcbs[i];
        }
    }
    return NULL;
}
static inline pc_net_err pc_net_udp_bind(pc_udp_pcb *p, const pc_net_ip *a, uint16_t port)
{
    (void)a;
    if (!p)
    {
        return PC_NET_ERR_ARG;
    }
    p->local_port = port;
    return PC_NET_OK;
}
static inline void pc_net_udp_recv(pc_udp_pcb *p, pc_net_udp_recv_fn fn, void *arg)
{
    if (p)
    {
        p->on_recv = fn;
        p->arg = arg;
    }
}
static inline pc_net_err pc_net_udp_sendto(pc_udp_pcb *p, pc_pbuf *b, const pc_net_ip *a, uint16_t port)
{
    (void)p;
    (void)b;
    (void)a;
    (void)port;
    return PC_NET_OK;
}
static inline void pc_net_udp_remove(pc_udp_pcb *p)
{
    if (p)
    {
        p->in_use = 0;
    }
}
#define PC_NET_HAS_IGMP 1

static inline pc_net_err pc_net_igmp_join(const pc_net_ip *nif, const pc_net_ip *grp)
{
    (void)nif;
    (void)grp;
    return PC_NET_OK;
}
static inline pc_net_err pc_net_igmp_leave(const pc_net_ip *nif, const pc_net_ip *grp)
{
    (void)nif;
    (void)grp;
    return PC_NET_OK;
}

// ---------------------------------------------------------------------------
// Test entry points
// ---------------------------------------------------------------------------

/** @brief Bytes the core has written since the last reset, and how many. */
static inline const uint8_t *pc_net_host_sent(size_t *len)
{
    if (len)
    {
        *len = pc_net_host_tx_len;
    }
    return pc_net_host_tx;
}

/** @brief Drop the capture and every pcb, so each test starts from a known state. */
static inline void pc_net_host_reset(void)
{
    pc_net_host_tx_len = 0;
    memset(pc_net_host_pcbs, 0, sizeof(pc_net_host_pcbs));
    memset(pc_net_host_udp_pcbs, 0, sizeof(pc_net_host_udp_pcbs));
}

// The same capture read as text. A response is a string for most of the suite - it asserts on a
// status line or a header - so this NUL-terminates what was written and hands it back. The write
// path bounds the length against the buffer, so there is always a byte left for the terminator.
static inline void tcp_capture_reset(void)
{
    pc_net_host_tx_len = 0;
    pc_net_host_tx[0] = '\0';
    pc_net_host_write_fail_after = -1; // clear a send failure a prior test armed
}

// The host always captures, so there is no capture to switch off. What a caller wants here is to
// stop collecting and then read what was collected, so the buffer is left intact.
static inline void tcp_capture_disable(void)
{
}

static inline const char *tcp_captured(void)
{
    size_t n = pc_net_host_tx_len < sizeof(pc_net_host_tx) ? pc_net_host_tx_len : sizeof(pc_net_host_tx) - 1;
    pc_net_host_tx[n] = '\0';
    return (const char *)pc_net_host_tx;
}

static inline size_t tcp_captured_len(void)
{
    return pc_net_host_tx_len;
}

/** @brief Deliver @p n bytes to @p p's recv callback as one segment. */
static inline pc_net_err pc_net_host_deliver(pc_pcb *p, void *data, uint16_t n)
{
    if (!p || !p->on_recv)
    {
        return PC_NET_ERR_ARG;
    }
    pc_pbuf b;
    memset(&b, 0, sizeof(b));
    b.payload = data;
    b.len = n;
    b.tot_len = n;
    return p->on_recv(p->arg, p, &b, PC_NET_OK);
}

/** @brief Deliver a peer FIN (a null pbuf) to @p p's recv callback. */
static inline pc_net_err pc_net_host_close_peer(pc_pcb *p)
{
    if (!p || !p->on_recv)
    {
        return PC_NET_ERR_ARG;
    }
    return p->on_recv(p->arg, p, NULL, PC_NET_OK);
}

#endif // PROTOCORE_PC_NET_HOST_H
