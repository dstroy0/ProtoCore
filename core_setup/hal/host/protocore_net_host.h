// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host pcb driver: the target's scheduler + TCP/UDP surface, implemented for the test build.
//
// src/core_setup/board_profiles/protocore_platform.h aliases that surface onto the vendor's calls on
// the hot path. On the test build there is no vendor, so this file supplies the same names, the
// same shapes, and the same member layout the core reads. That is what lets a transport TU be
// compiled and driven on the host instead of only on silicon.
//
// Pcbs come from a fixed table, sends are captured, and callbacks are
// stored so a test can fire them. Nothing here talks to a socket. A test that wants behavior
// drives it through the protocore_net_host_* entry points at the bottom.
//
// Every mutable global below carries external linkage through a shared definition rather than
// `static`. A `static` in a header is one object PER TRANSLATION UNIT, so the core wrote its own
// copy of the send capture while the test read a different one and found it empty - the state is
// only a seam if both sides reach the same bytes. PROTOCORE_HOST_SHARED lets every TU emit the
// same definition and the linker keep exactly one.
//
// test/ is exempt from the src/ style rules, so this reads as plain host C.

#ifndef PROTOCORE_PROTOCORE_NET_HOST_H
#define PROTOCORE_PROTOCORE_NET_HOST_H

#include <Arduino.h> // the virtual clock the host time base reads: millis() / set_millis()
#include <setjmp.h>  // how a task entry that never returns is unwound; see the task table below
#include <stddef.h>
#include <stdint.h>
#include <string.h> // memcpy / memset: the staging copies below

#include <time.h>

// ---------------------------------------------------------------------------
// Buses
// ---------------------------------------------------------------------------
//
// Loopback, not silence: what the core writes is captured and can be read back, and a test can
// preload what a read should return. That makes the bridge's framing and transaction logic
// exercisable on the host instead of only on a wired rig.

#define PROTOCORE_UART_UNITS 3

#ifndef PROTOCORE_BUS_HOST_CAP
#define PROTOCORE_BUS_HOST_CAP 1024
#endif

// One definition per name across every TU that includes this, kept by the linker: selectany on
// PE/COFF, weak on ELF.
#ifndef PROTOCORE_HOST_SHARED
#if defined(_WIN32)
#define PROTOCORE_HOST_SHARED __declspec(selectany)
#else
#define PROTOCORE_HOST_SHARED __attribute__((weak))
#endif
#endif

PROTOCORE_HOST_SHARED uint8_t protocore_bus_host_tx[PROTOCORE_BUS_HOST_CAP];
PROTOCORE_HOST_SHARED uint32_t protocore_bus_host_tx_len;
PROTOCORE_HOST_SHARED uint8_t protocore_bus_host_rx[PROTOCORE_BUS_HOST_CAP];
PROTOCORE_HOST_SHARED uint32_t protocore_bus_host_rx_len;
PROTOCORE_HOST_SHARED uint32_t protocore_bus_host_rx_pos;

static inline void protocore_bus_host_capture(const void *buf, uint32_t len)
{
    const uint8_t *b = (const uint8_t *)buf;
    while (len-- && protocore_bus_host_tx_len < PROTOCORE_BUS_HOST_CAP)
    {
        protocore_bus_host_tx[protocore_bus_host_tx_len++] = *b++;
    }
}
static inline uint32_t protocore_bus_host_drain(uint8_t *buf, uint32_t len)
{
    uint32_t n = 0;
    while (n < len && protocore_bus_host_rx_pos < protocore_bus_host_rx_len)
    {
        buf[n++] = protocore_bus_host_rx[protocore_bus_host_rx_pos++];
    }
    return n;
}

// Per-transaction log. The concatenated write stream says what went out; this says where one
// transfer ended and the next began, which bus ran it, and which device it addressed. A driver
// that splits a frame across two transfers, or addresses the wrong part, is only visible here.

#define PROTOCORE_BUS_HOST_I2C 0
#define PROTOCORE_BUS_HOST_SPI 1
#define PROTOCORE_BUS_HOST_UART 2

#ifndef PROTOCORE_BUS_HOST_MAX_TXN
#define PROTOCORE_BUS_HOST_MAX_TXN 192
#endif

typedef struct
{
    uint8_t kind;    /**< PROTOCORE_BUS_HOST_I2C / _SPI / _UART */
    uint16_t target; /**< I2C address, SPI host, or UART unit */
    uint32_t woff;   /**< where this transfer's bytes start in the write stream */
    uint32_t wlen;
    uint32_t rlen;
    uint32_t t_us; /**< when it ran, so the gap to the next one is assertable */
} protocore_bus_host_rec;

PROTOCORE_HOST_SHARED protocore_bus_host_rec protocore_bus_host_log[PROTOCORE_BUS_HOST_MAX_TXN];
PROTOCORE_HOST_SHARED uint32_t protocore_bus_host_log_len;
PROTOCORE_HOST_SHARED uint32_t protocore_bus_host_fail;

/* Defined with the time base further down; the log stamps every transfer with it. */
static inline uint32_t protocore_platform_micros(void);

/** @brief Record one transfer and capture its write span. 0 when a test asked it to fail. */
static inline int protocore_bus_host_record(uint8_t kind, uint16_t target, const void *w, uint32_t wlen, uint32_t rlen)
{
    if (protocore_bus_host_fail > 0)
    {
        protocore_bus_host_fail--;
        return 0;
    }
    if (protocore_bus_host_log_len < PROTOCORE_BUS_HOST_MAX_TXN)
    {
        protocore_bus_host_rec *t = &protocore_bus_host_log[protocore_bus_host_log_len++];
        t->kind = kind;
        t->target = target;
        t->woff = protocore_bus_host_tx_len; /* before the capture below moves it */
        t->wlen = wlen;
        t->rlen = rlen;
        t->t_us = protocore_platform_micros();
    }
    if (w && wlen)
    {
        protocore_bus_host_capture(w, wlen);
    }
    return 1;
}

/** @brief Microseconds between transfer @p a and transfer @p b, for a settle-time assertion. */
static inline uint32_t protocore_bus_host_gap_us(uint32_t a, uint32_t b)
{
    if (a >= protocore_bus_host_log_len || b >= protocore_bus_host_log_len)
    {
        return 0;
    }
    return protocore_bus_host_log[b].t_us - protocore_bus_host_log[a].t_us;
}

/** @brief How many transfers ran since the last reset. */
static inline uint32_t protocore_bus_host_count(void)
{
    return protocore_bus_host_log_len;
}

/** @brief Transfer @p i, or NULL past the end. */
static inline const protocore_bus_host_rec *protocore_bus_host_txn_at(uint32_t i)
{
    return (i < protocore_bus_host_log_len) ? &protocore_bus_host_log[i] : (const protocore_bus_host_rec *)0;
}

/** @brief The bytes transfer @p i wrote, or NULL past the end. */
static inline const uint8_t *protocore_bus_host_txn_bytes(uint32_t i, uint32_t *len)
{
    if (i >= protocore_bus_host_log_len)
    {
        return (const uint8_t *)0;
    }
    if (len)
    {
        *len = protocore_bus_host_log[i].wlen;
    }
    return &protocore_bus_host_tx[protocore_bus_host_log[i].woff];
}

/** @brief Make the next @p n transfers fail, as a device that does not acknowledge would. */
static inline void protocore_bus_host_fail_next(uint32_t n)
{
    protocore_bus_host_fail = n;
}

// Tells the bus owners a seam exists, so their host arm drives it and the capture below sees what
// a driver actually composed rather than the owner refusing before the driver got that far.
#define PROTOCORE_PLATFORM_HAS_BUS 1

// The vendor seams core_setup/hal/host/host_platform.c answers for. Declared here because this is
// the header protocore_platform.h pulls in on the host arm, so the gates it computes below can read
// them; the implementations and the setters a test states a fact through are in host_platform.c/.h.
// Each one turns the owner's real hardware path on, so a host suite exercises the same code the
// device runs instead of the refusing arm.
#define PROTOCORE_PLATFORM_HAS_VENDOR_MAC 1
#define PROTOCORE_PLATFORM_HAS_VENDOR_HEAP_INFO 1
#define PROTOCORE_PLATFORM_HAS_VENDOR_PM 1
#define PROTOCORE_PLATFORM_HAS_VENDOR_BT 1
#define PROTOCORE_PLATFORM_HAS_VENDOR_OTA 1
#define PROTOCORE_PLATFORM_HAS_VENDOR_COREDUMP 1
#define PROTOCORE_PLATFORM_HAS_VENDOR_CAN 1

static inline int protocore_platform_uart_begin(uint8_t unit, uint32_t baud, int rx, int tx)
{
    (void)baud;
    (void)rx;
    (void)tx;
    return (unit < PROTOCORE_UART_UNITS) ? 1 : 0;
}
static inline int protocore_platform_uart_write(uint8_t unit, const void *buf, uint32_t len)
{
    return protocore_bus_host_record(PROTOCORE_BUS_HOST_UART, unit, buf, len, 0) ? (int)len : 0;
}
static inline int protocore_platform_uart_read(uint8_t unit, void *buf, uint32_t len, uint32_t ms)
{
    (void)ms;
    if (!protocore_bus_host_record(PROTOCORE_BUS_HOST_UART, unit, 0, 0, len))
    {
        return 0;
    }
    return (int)protocore_bus_host_drain((uint8_t *)buf, len);
}
static inline uint32_t protocore_platform_uart_available(uint8_t unit)
{
    (void)unit;
    return protocore_bus_host_rx_len - protocore_bus_host_rx_pos;
}

#define PROTOCORE_SPI_MSBFIRST 0
#define PROTOCORE_SPI_LSBFIRST 1

#define PROTOCORE_SPI_LANES_1 1
#define PROTOCORE_SPI_LANES_2 2
#define PROTOCORE_SPI_LANES_4 4

PROTOCORE_HOST_SHARED int protocore_spi_host_up;

static inline int protocore_platform_spi_begin(uint8_t host, int mosi, int miso, int sclk, int quadwp, int quadhd)
{
    (void)host;
    (void)mosi;
    (void)miso;
    (void)sclk;
    (void)quadwp;
    (void)quadhd;
    protocore_spi_host_up = 1;
    return 1;
}
static inline int protocore_platform_spi_txn(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode,
                                             const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    (void)host;
    (void)hz;
    (void)bit_order;
    (void)mode;
    if (!protocore_spi_host_up)
    {
        return 0;
    }
    if (!protocore_bus_host_record(PROTOCORE_BUS_HOST_SPI, host, tx, tx ? len : 0u, rx ? len : 0u))
    {
        return 0;
    }
    if (rx)
    {
        uint32_t got = protocore_bus_host_drain(rx, len);
        while (got < len)
        {
            rx[got++] = 0;
        }
    }
    return 1;
}
static inline int protocore_platform_spi_txn_ext(uint8_t host, uint32_t hz, uint8_t bit_order, uint8_t mode,
                                                 uint16_t cmd, uint8_t cmd_bits, uint32_t addr, uint8_t addr_bits,
                                                 uint8_t dummy_bits, uint8_t lanes, const uint8_t *tx, uint8_t *rx,
                                                 uint32_t len)
{
    (void)cmd;
    (void)cmd_bits;
    (void)addr;
    (void)addr_bits;
    (void)dummy_bits;
    (void)lanes;
    return protocore_platform_spi_txn(host, hz, bit_order, mode, tx, rx, len);
}

// ---------------------------------------------------------------------------
// Device models
// ---------------------------------------------------------------------------
//
// A part attached to an address answers its own transfers. The capture above says what the core
// drove out; a model says what comes back, computed from the bytes this transfer carried, so a
// read is the part's response to what the driver just wrote rather than a queue filled in
// transfer order. Each model lives in its own header under devices/ and holds its register file
// in a context the suite owns.

#ifndef PROTOCORE_BUS_HOST_MAX_DEV
#define PROTOCORE_BUS_HOST_MAX_DEV 8
#endif

// One transfer as the part sees it: wlen bytes arrived, rlen are wanted back. Returns how many it
// supplied; the seam zero-fills the rest.
typedef uint32_t (*protocore_bus_host_dev_fn)(void *ctx, const uint8_t *w, uint32_t wlen, uint8_t *r, uint32_t rlen);

typedef struct
{
    protocore_bus_host_dev_fn txn; /**< null in a free slot */
    void *ctx;                     /**< the model's register file, owned by whoever attached it */
    uint16_t addr;
    uint8_t kind; /**< PROTOCORE_BUS_HOST_I2C / _SPI / _UART */
} protocore_bus_host_dev;

PROTOCORE_HOST_SHARED protocore_bus_host_dev protocore_bus_host_dev_tbl[PROTOCORE_BUS_HOST_MAX_DEV];

/** @brief The model at one address on one bus, or null. */
static inline protocore_bus_host_dev *protocore_bus_host_dev_at(uint8_t kind, uint16_t addr)
{
    for (uint32_t i = 0; i < PROTOCORE_BUS_HOST_MAX_DEV; i++)
    {
        protocore_bus_host_dev *d = &protocore_bus_host_dev_tbl[i];
        if (d->txn && d->kind == kind && d->addr == addr)
        {
            return d;
        }
    }
    return 0;
}

/** @brief Put a model at one address, replacing any already there. 0 when the table is full. */
static inline int protocore_bus_host_attach(uint8_t kind, uint16_t addr, void *ctx, protocore_bus_host_dev_fn txn)
{
    protocore_bus_host_dev *d = protocore_bus_host_dev_at(kind, addr);
    for (uint32_t i = 0; i < PROTOCORE_BUS_HOST_MAX_DEV && d == 0; i++)
    {
        if (protocore_bus_host_dev_tbl[i].txn == 0)
        {
            d = &protocore_bus_host_dev_tbl[i];
        }
    }
    if (d == 0)
    {
        return 0;
    }
    d->kind = kind;
    d->addr = addr;
    d->ctx = ctx;
    d->txn = txn;
    return 1;
}

/** @brief Take every model off every bus. */
static inline void protocore_bus_host_detach_all(void)
{
    for (uint32_t i = 0; i < PROTOCORE_BUS_HOST_MAX_DEV; i++)
    {
        protocore_bus_host_dev_tbl[i].txn = 0;
    }
}

// What a transfer reads back: the model at that address if one is attached, otherwise the
// preloaded queue, zero-filled to the length asked for either way. A write is the same call with
// rlen 0, which is how a model sees the bytes written to it.
static inline void protocore_bus_host_answer(uint8_t kind, uint16_t addr, const uint8_t *w, uint32_t wlen, uint8_t *r,
                                             uint32_t rlen)
{
    protocore_bus_host_dev *d = protocore_bus_host_dev_at(kind, addr);
    uint32_t got = d ? d->txn(d->ctx, w, wlen, r, rlen) : protocore_bus_host_drain(r, rlen);
    while (got < rlen)
    {
        r[got++] = 0;
    }
}

// I2C. The address a transfer went to is recorded ahead of its payload, so a suite can tell one
// device's traffic from another's on a shared bus.
#define PROTOCORE_I2C_ADDR_10BIT 0x8000u
#define PROTOCORE_I2C_ADDR_MASK 0x03FFu
#define PROTOCORE_I2C_GENERAL_CALL 0x00u

PROTOCORE_HOST_SHARED int protocore_i2c_host_up;
PROTOCORE_HOST_SHARED uint16_t protocore_bus_host_last_addr;

static inline int protocore_platform_i2c_begin(uint8_t bus, int sda, int scl, uint32_t hz)
{
    (void)bus;
    (void)sda;
    (void)scl;
    (void)hz;
    protocore_i2c_host_up = 1;
    return 1;
}
static inline int protocore_platform_i2c_write(uint8_t bus, uint16_t addr, const uint8_t *buf, uint32_t len,
                                               uint32_t ms)
{
    (void)bus;
    (void)ms;
    protocore_bus_host_last_addr = addr;
    if (!protocore_bus_host_record(PROTOCORE_BUS_HOST_I2C, addr, buf, len, 0))
    {
        return 0;
    }
    protocore_bus_host_answer(PROTOCORE_BUS_HOST_I2C, addr, buf, len, 0, 0);
    return 1;
}
static inline int protocore_platform_i2c_read(uint8_t bus, uint16_t addr, uint8_t *buf, uint32_t len, uint32_t ms)
{
    (void)bus;
    (void)ms;
    protocore_bus_host_last_addr = addr;
    if (!protocore_bus_host_record(PROTOCORE_BUS_HOST_I2C, addr, 0, 0, len))
    {
        return 0;
    }
    protocore_bus_host_answer(PROTOCORE_BUS_HOST_I2C, addr, 0, 0, buf, len);
    return 1;
}
static inline int protocore_platform_i2c_write_read(uint8_t bus, uint16_t addr, const uint8_t *w, uint32_t wlen,
                                                    uint8_t *r, uint32_t rlen, uint32_t ms)
{
    (void)bus;
    (void)ms;
    protocore_bus_host_last_addr = addr;
    if (!protocore_bus_host_record(PROTOCORE_BUS_HOST_I2C, addr, w, wlen, rlen))
    {
        return 0;
    }
    protocore_bus_host_answer(PROTOCORE_BUS_HOST_I2C, addr, w, wlen, r, rlen);
    return 1;
}
static inline int protocore_platform_i2c_set_clock(uint8_t bus, uint32_t hz)
{
    (void)bus;
    (void)hz;
    return 1;
}
// A probe is an address cycle with no payload. An attached model answers it; a bare address does
// not, so a scan finds exactly the parts a suite put on the bus.
static inline int protocore_platform_i2c_probe(uint8_t bus, uint16_t addr, uint32_t ms)
{
    (void)bus;
    (void)ms;
    protocore_bus_host_last_addr = addr;
    if (!protocore_bus_host_record(PROTOCORE_BUS_HOST_I2C, addr, 0, 0, 0))
    {
        return 0;
    }
    return protocore_bus_host_dev_at(PROTOCORE_BUS_HOST_I2C, addr) != 0;
}
static inline int protocore_platform_i2c_recover(uint8_t bus, int sda, int scl)
{
    (void)bus;
    (void)sda;
    (void)scl;
    return 1;
}

/** @brief Bytes the core has driven onto any bus since the last reset. */
static inline const uint8_t *protocore_bus_host_written(uint32_t *len)
{
    if (len)
    {
        *len = protocore_bus_host_tx_len;
    }
    return protocore_bus_host_tx;
}
/** @brief Preload what the next bus reads return. */
static inline void protocore_bus_host_preload(const uint8_t *data, uint32_t len)
{
    protocore_bus_host_rx_len = (len > PROTOCORE_BUS_HOST_CAP) ? PROTOCORE_BUS_HOST_CAP : len;
    protocore_bus_host_rx_pos = 0;
    for (uint32_t i = 0; i < protocore_bus_host_rx_len; i++)
    {
        protocore_bus_host_rx[i] = data[i];
    }
}
/** @brief Drop both directions so each test starts clean. */
static inline void protocore_bus_host_reset(void)
{
    protocore_bus_host_tx_len = 0;
    protocore_bus_host_rx_len = 0;
    protocore_bus_host_rx_pos = 0;
    protocore_bus_host_log_len = 0;
    protocore_bus_host_fail = 0;
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
// protocore_rand_host_seed() makes a run repeatable from a chosen point.

PROTOCORE_HOST_SHARED uint32_t protocore_rand_host_state = 0x2545F491u;

static inline uint32_t protocore_platform_rand_u32(void)
{
    uint32_t x = protocore_rand_host_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    protocore_rand_host_state = x;
    return x;
}
static inline void protocore_platform_rand_fill(void *buf, size_t len)
{
    uint8_t *b = (uint8_t *)buf;
    while (len >= 4)
    {
        uint32_t v = protocore_platform_rand_u32();
        b[0] = (uint8_t)v;
        b[1] = (uint8_t)(v >> 8);
        b[2] = (uint8_t)(v >> 16);
        b[3] = (uint8_t)(v >> 24);
        b += 4;
        len -= 4;
    }
    uint32_t v = protocore_platform_rand_u32();
    while (len--)
    {
        *b++ = (uint8_t)v;
        v >>= 8;
    }
}

/** @brief Restart the host generator from @p seed so a run reproduces. */
static inline void protocore_rand_host_seed(uint32_t seed)
{
    protocore_rand_host_state = seed ? seed : 0x2545F491u;
}

// ---------------------------------------------------------------------------
// GPIO
// ---------------------------------------------------------------------------
//
// A pin table rather than a no-op: a test sets an input level with protocore_gpio_host_set() and reads
// back what the core drove with protocore_gpio_host_level(), so pin logic is exercised on the host.

// Tells the pin drivers a seam exists, the same way PROTOCORE_PLATFORM_HAS_BUS does for the bus owners.
#define PROTOCORE_PLATFORM_HAS_GPIO 1

#define PROTOCORE_GPIO_IN 0
#define PROTOCORE_GPIO_OUT 1
#define PROTOCORE_GPIO_IN_PULLUP 2
#define PROTOCORE_GPIO_IN_PULLDOWN 3
#define PROTOCORE_GPIO_LOW 0
#define PROTOCORE_GPIO_HIGH 1

#ifndef PROTOCORE_GPIO_HOST_PINS
#define PROTOCORE_GPIO_HOST_PINS 64
#endif
PROTOCORE_HOST_SHARED uint8_t protocore_gpio_host_mode_tbl[PROTOCORE_GPIO_HOST_PINS];
PROTOCORE_HOST_SHARED uint8_t protocore_gpio_host_level_tbl[PROTOCORE_GPIO_HOST_PINS];

static inline void protocore_platform_gpio_mode(uint8_t pin, uint8_t mode)
{
    if (pin < PROTOCORE_GPIO_HOST_PINS)
    {
        protocore_gpio_host_mode_tbl[pin] = mode;
        if (mode == PROTOCORE_GPIO_IN_PULLUP)
        {
            protocore_gpio_host_level_tbl[pin] = 1;
        }
        else if (mode == PROTOCORE_GPIO_IN_PULLDOWN)
        {
            protocore_gpio_host_level_tbl[pin] = 0;
        }
    }
}
static inline void protocore_platform_gpio_write(uint8_t pin, uint8_t level)
{
    if (pin < PROTOCORE_GPIO_HOST_PINS)
    {
        protocore_gpio_host_level_tbl[pin] = level ? 1 : 0;
    }
}
static inline uint8_t protocore_platform_gpio_read(uint8_t pin)
{
    return (pin < PROTOCORE_GPIO_HOST_PINS) ? protocore_gpio_host_level_tbl[pin] : 0;
}

/** @brief Drive an input pin from a test. */
static inline void protocore_gpio_host_set(uint8_t pin, uint8_t level)
{
    protocore_platform_gpio_write(pin, level);
}
/** @brief What the core last drove (or a test last set) on @p pin. */
static inline uint8_t protocore_gpio_host_level(uint8_t pin)
{
    return protocore_platform_gpio_read(pin);
}
/** @brief The direction the core configured for @p pin. */
static inline uint8_t protocore_gpio_host_mode(uint8_t pin)
{
    return (pin < PROTOCORE_GPIO_HOST_PINS) ? protocore_gpio_host_mode_tbl[pin] : 0;
}

// ---------------------------------------------------------------------------
// Time base
// ---------------------------------------------------------------------------
//
// The host has no tick timer, so the platform default is the virtual clock in the Arduino mock:
// set_millis() moves it and nothing else does. That makes the default path deterministic, which
// is what a timeout test drives when it reverts an override with protocore_set_clock(NULL, 0).

static inline uint32_t protocore_platform_millis(void)
{
    return millis();
}

// Microseconds advance one per read on top of the millisecond base. The host has no timer below a
// millisecond, so a sub-millisecond spin (a part's settle time, protocore_delay_us) would never see the
// counter move and would not terminate. Advancing per read makes such a wait finish in bounded
// time and makes the elapsed value deterministic, which is what a jitter assertion needs.
PROTOCORE_HOST_SHARED uint32_t protocore_host_us_tick;

static inline uint32_t protocore_platform_micros(void)
{
    protocore_host_us_tick++;
    return millis() * 1000u + protocore_host_us_tick;
}
// The cycle counter is the one time seam that must track real elapsed time: it is what the
// microbenchmarks measure with, and millis() above is a virtual clock that only set_millis() moves.
// A monotonic read scaled to PROTOCORE_HOST_CYCLE_MHZ gives a count in the same units the benches
// divide by, so a host figure is wall time expressed at the nominal clock, not a silicon cycle
// count. It wraps at 2^32 like the hardware counter, so only short deltas are meaningful.
#ifndef PROTOCORE_HOST_CYCLE_MHZ
#define PROTOCORE_HOST_CYCLE_MHZ 240u
#endif

static inline uint32_t protocore_platform_cycles(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return protocore_platform_micros() * PROTOCORE_HOST_CYCLE_MHZ;
    }
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return (uint32_t)((ns * PROTOCORE_HOST_CYCLE_MHZ) / 1000ull);
}

// ---------------------------------------------------------------------------
// Reboot
// ---------------------------------------------------------------------------
//
// On silicon this does not return: the vendor arm is esp_restart(). A host process cannot restart
// itself and must not exit, or the suite that asked for it dies before it can assert anything - so
// the request is recorded and the caller carries on. What a test wants to know is that the core
// asked, and how many times.

PROTOCORE_HOST_SHARED uint32_t protocore_host_restarts;

static inline void protocore_platform_restart(void)
{
    protocore_host_restarts++;
}

/** @brief How many times the core has asked for a reboot since the last reset. */
static inline uint32_t protocore_host_restart_count(void)
{
    return protocore_host_restarts;
}
static inline void protocore_host_restart_reset(void)
{
    protocore_host_restarts = 0;
}

// ---------------------------------------------------------------------------
// Scheduler surface
// ---------------------------------------------------------------------------

// Tells the worker layer a scheduler exists, the same way PROTOCORE_PLATFORM_HAS_BUS does for the
// bus owners: the queues and tasks below are the seam, so the pipeline's worker path runs here
// instead of resolving to the inline arm.
#define PROTOCORE_PLATFORM_HAS_SCHEDULER 1

typedef void *protocore_platform_queue;
typedef struct
{
    uint8_t opaque[8];
} protocore_platform_queue_ctrl;
typedef void *protocore_platform_task;
typedef void (*protocore_platform_task_fn)(void *);
typedef int protocore_platform_status;
typedef uint32_t protocore_platform_ticks;

// A real lock, not a stand-in. The one caller (ssh_rsa) serializes signs on a single shared mbedtls
// context because the sign mutates it, so a host arm that only pretended to lock would run the case
// the mutex exists to prevent and still pass. The control block is the caller's, in BSS, the same
// as the static FreeRTOS mutex it stands for.
#include <pthread.h> // PROTOCORE_ALLOW_LATE_INCLUDE: host only, and only the scheduler surface uses it

typedef void *protocore_platform_mutex;
typedef struct
{
    pthread_mutex_t m;
    int ready; ///< the block has been initialised; take/give on an uninitialised one refuses
} protocore_platform_mutex_ctrl;

static inline protocore_platform_mutex protocore_platform_mutex_create(protocore_platform_mutex_ctrl *ctrl)
{
    if (!ctrl || pthread_mutex_init(&ctrl->m, NULL) != 0)
    {
        return (protocore_platform_mutex)0;
    }
    ctrl->ready = 1;
    return (protocore_platform_mutex)ctrl;
}

/// Blocks until the holder releases. The tick budget is the caller's on silicon; here it waits.
static inline int protocore_platform_mutex_take(protocore_platform_mutex h, uint32_t ticks)
{
    (void)ticks;
    protocore_platform_mutex_ctrl *c = (protocore_platform_mutex_ctrl *)h;
    if (!c || !c->ready)
    {
        return 0;
    }
    return pthread_mutex_lock(&c->m) == 0;
}

static inline int protocore_platform_mutex_give(protocore_platform_mutex h)
{
    protocore_platform_mutex_ctrl *c = (protocore_platform_mutex_ctrl *)h;
    if (!c || !c->ready)
    {
        return 0;
    }
    return pthread_mutex_unlock(&c->m) == 0;
}

#define PROTOCORE_PLATFORM_OK 1
#define PROTOCORE_PLATFORM_PASS 1
#define PROTOCORE_PLATFORM_FALSE 0
#define PROTOCORE_PLATFORM_WAIT_FOREVER 0xFFFFFFFFu
#define PROTOCORE_PLATFORM_CORES 1

// Where a task entry is unwound to. A task entry does not return: it loops until the kernel deletes
// it while it is blocked. Deleting a task abandons its frame, so the host does the same with
// longjmp - the entry runs until it would block and control resumes in protocore_platform_host_task_run.
// The depth is 0 outside a run, so a blocking call made by ordinary code cannot unwind.
PROTOCORE_HOST_SHARED jmp_buf protocore_task_host_jmp;
PROTOCORE_HOST_SHARED int protocore_task_host_depth;
PROTOCORE_HOST_SHARED int protocore_task_host_budget;

// How many timed waits one run allows before it unwinds. A task that waits on a timeout rather than
// forever would spin, since nothing else runs here to make its condition true.
#ifndef PROTOCORE_TASK_HOST_WAITS
#define PROTOCORE_TASK_HOST_WAITS 1
#endif

static inline void protocore_task_host_yield(void)
{
    if (protocore_task_host_depth > 0)
    {
        longjmp(protocore_task_host_jmp, 1);
    }
}

// Keyed by the handle create hands back, so an enqueue is observable at its consumer and
// per-worker routing is checkable here rather than only on hardware.
#define PROTOCORE_QUEUE_MAX 24
#define PROTOCORE_QUEUE_DEPTH 32
#define PROTOCORE_QUEUE_ITEM 64

typedef struct
{
    void *handle[PROTOCORE_QUEUE_MAX];
    size_t item[PROTOCORE_QUEUE_MAX];
    size_t depth[PROTOCORE_QUEUE_MAX]; // what create asked for, so a full queue fills where the core says
    size_t head[PROTOCORE_QUEUE_MAX];
    size_t tail[PROTOCORE_QUEUE_MAX];
    uint8_t buf[PROTOCORE_QUEUE_MAX][PROTOCORE_QUEUE_DEPTH * PROTOCORE_QUEUE_ITEM];
} PcQueueTable;
PROTOCORE_HOST_SHARED PcQueueTable g_protocore_queues;

static inline int protocore_queue_slot(protocore_platform_queue q)
{
    for (int i = 0; i < PROTOCORE_QUEUE_MAX; i++)
    {
        if (g_protocore_queues.handle[i] == q)
        {
            return i;
        }
    }
    return -1;
}

// One-shot creation failure: the next queue create reports no room, the way a kernel out of
// queue objects does, so the caller has to unwind the listener it was building.
PROTOCORE_HOST_SHARED int protocore_platform_queue_create_fail_once;

static inline void mock_queue_create_fail_once(void)
{
    protocore_platform_queue_create_fail_once = 1;
}

static inline protocore_platform_queue protocore_platform_queue_create(size_t depth, size_t item, void *storage,
                                                                       void *ctrl)
{
    (void)depth;
    (void)ctrl;
    if (protocore_platform_queue_create_fail_once)
    {
        protocore_platform_queue_create_fail_once = 0;
        return NULL;
    }
    protocore_platform_queue h = storage;
    if (h == NULL)
    {
        h = (void *)1;
    }
    size_t sz = item;
    if (sz > PROTOCORE_QUEUE_ITEM)
    {
        sz = PROTOCORE_QUEUE_ITEM;
    }
    size_t d = depth;
    if (d == 0 || d > PROTOCORE_QUEUE_DEPTH)
    {
        d = PROTOCORE_QUEUE_DEPTH;
    }
    int slot = protocore_queue_slot(h);
    if (slot < 0)
    {
        slot = protocore_queue_slot(NULL);
    }
    if (slot >= 0)
    {
        g_protocore_queues.handle[slot] = h;
        g_protocore_queues.item[slot] = sz;
        g_protocore_queues.depth[slot] = d;
        g_protocore_queues.head[slot] = 0;
        g_protocore_queues.tail[slot] = 0;
    }
    return h;
}
// One-shot send failure: the next protocore_platform_queue_send() reports a full queue and clears the
// latch. Lets a test drive the enqueue path's rejection branch.
PROTOCORE_HOST_SHARED int protocore_platform_queue_send_fail_once = 0;

static inline void mock_queue_send_fail_once(void)
{
    protocore_platform_queue_send_fail_once = 1;
}

// The two send entry points differ only in which end they write, so the bounds and the copy live here.
static inline int protocore_queue_push(protocore_platform_queue q, const void *item, size_t at)
{
    int slot = protocore_queue_slot(q);
    if (slot < 0 || item == NULL)
    {
        return PROTOCORE_PLATFORM_FALSE;
    }
    if ((g_protocore_queues.head[slot] - g_protocore_queues.tail[slot]) >= g_protocore_queues.depth[slot])
    {
        return PROTOCORE_PLATFORM_FALSE; // full at the depth create asked for, the way a kernel queue refuses
    }
    memcpy(&g_protocore_queues.buf[slot][(at % PROTOCORE_QUEUE_DEPTH) * PROTOCORE_QUEUE_ITEM], item,
           g_protocore_queues.item[slot]);
    return PROTOCORE_PLATFORM_OK;
}

static inline int protocore_platform_queue_send(protocore_platform_queue q, const void *item, uint32_t ticks)
{
    (void)ticks;
    if (protocore_platform_queue_send_fail_once)
    {
        protocore_platform_queue_send_fail_once = 0;
        return PROTOCORE_PLATFORM_FALSE;
    }
    int slot = protocore_queue_slot(q);
    if (slot < 0)
    {
        return PROTOCORE_PLATFORM_FALSE;
    }
    int ok = protocore_queue_push(q, item, g_protocore_queues.head[slot]);
    if (ok == PROTOCORE_PLATFORM_OK)
    {
        g_protocore_queues.head[slot]++;
    }
    return ok;
}

static inline int protocore_platform_queue_send_front(protocore_platform_queue q, const void *item, uint32_t ticks)
{
    (void)ticks;
    int slot = protocore_queue_slot(q);
    if (slot < 0)
    {
        return PROTOCORE_PLATFORM_FALSE;
    }
    int ok = protocore_queue_push(q, item, g_protocore_queues.tail[slot] - 1);
    if (ok == PROTOCORE_PLATFORM_OK)
    {
        g_protocore_queues.tail[slot]--;
    }
    return ok;
}

static inline int protocore_platform_queue_send_isr(protocore_platform_queue q, const void *item, int *woke)
{
    if (woke)
    {
        *woke = 0;
    }
    return protocore_platform_queue_send(q, item, 0);
}

// Called from setUp so one case cannot inherit another's backlog.
static inline void queue_stage_reset(void)
{
    for (int i = 0; i < PROTOCORE_QUEUE_MAX; i++)
    {
        g_protocore_queues.head[i] = 0;
        g_protocore_queues.tail[i] = 0;
    }
}

static inline int protocore_platform_queue_recv(protocore_platform_queue q, void *item, uint32_t ticks)
{
    int slot = protocore_queue_slot(q);
    if (slot < 0 || item == NULL || g_protocore_queues.head[slot] == g_protocore_queues.tail[slot])
    {
        // A wait-forever on an empty queue is where a task parks. Nothing else runs here to fill
        // it, so the entry unwinds instead; a timed or polling receive just reports empty.
        if (ticks == PROTOCORE_PLATFORM_WAIT_FOREVER)
        {
            protocore_task_host_yield();
        }
        return 0;
    }
    memcpy(
        item,
        &g_protocore_queues.buf[slot][(g_protocore_queues.tail[slot] % PROTOCORE_QUEUE_DEPTH) * PROTOCORE_QUEUE_ITEM],
        g_protocore_queues.item[slot]);
    g_protocore_queues.tail[slot]++;
    return PROTOCORE_PLATFORM_OK;
}
static inline size_t protocore_platform_queue_waiting(protocore_platform_queue q)
{
    int slot = protocore_queue_slot(q);
    if (slot < 0)
    {
        return 0;
    }
    return g_protocore_queues.head[slot] - g_protocore_queues.tail[slot];
}
static inline size_t protocore_platform_queue_waiting_isr(protocore_platform_queue q)
{
    return protocore_platform_queue_waiting(q);
}
static inline void protocore_platform_queue_delete(protocore_platform_queue q)
{
    int slot = protocore_queue_slot(q);
    if (slot >= 0)
    {
        g_protocore_queues.handle[slot] = NULL;
    }
}

// The started tasks, keyed by the handle create hands back, the same way the queue table above is.
// Nothing here runs on its own: the entry function is kept, and a test runs it when it chooses.
#define PROTOCORE_TASK_MAX 16

typedef struct
{
    protocore_platform_task_fn fn[PROTOCORE_TASK_MAX];
    void *arg[PROTOCORE_TASK_MAX];
    void *handle[PROTOCORE_TASK_MAX];
    const char *name[PROTOCORE_TASK_MAX]; // what start was given, so one task can be run by itself
    int started[PROTOCORE_TASK_MAX];
} PcTaskTable;
PROTOCORE_HOST_SHARED PcTaskTable g_protocore_tasks;

static inline int protocore_task_slot(protocore_platform_task t)
{
    for (int i = 0; i < PROTOCORE_TASK_MAX; i++)
    {
        if (g_protocore_tasks.started[i] && g_protocore_tasks.handle[i] == t)
        {
            return i;
        }
    }
    return -1;
}

// One-shot start failure: the next protocore_platform_task_start reports no room, the way a kernel out of
// task control blocks does, so the caller has to unwind what it was bringing up.
PROTOCORE_HOST_SHARED int protocore_platform_task_start_fail_once;

static inline void mock_task_start_fail_once(void)
{
    protocore_platform_task_start_fail_once = 1;
}

static inline int protocore_platform_task_start(protocore_platform_task_fn fn, const char *name, uint32_t stack,
                                                void *arg, int prio, protocore_platform_task *out, int core)
{
    (void)name;
    (void)stack;
    (void)prio;
    (void)core;
    if (protocore_platform_task_start_fail_once)
    {
        protocore_platform_task_start_fail_once = 0;
        return PROTOCORE_PLATFORM_FALSE;
    }
    for (int i = 0; i < PROTOCORE_TASK_MAX; i++)
    {
        if (!g_protocore_tasks.started[i])
        {
            g_protocore_tasks.fn[i] = fn;
            g_protocore_tasks.arg[i] = arg;
            g_protocore_tasks.name[i] = name;
            // The handle is the slot, offset so it is never NULL: the core tests a task handle
            // against NULL to decide there is one.
            g_protocore_tasks.handle[i] = (void *)(uintptr_t)(i + 1);
            g_protocore_tasks.started[i] = 1;
            if (out)
            {
                *out = g_protocore_tasks.handle[i];
            }
            return PROTOCORE_PLATFORM_PASS;
        }
    }
    return PROTOCORE_PLATFORM_FALSE;
}
static inline void protocore_platform_task_stop(protocore_platform_task t)
{
    int slot = protocore_task_slot(t);
    if (slot >= 0)
    {
        g_protocore_tasks.started[slot] = 0;
    }
}

/**
 * @brief Run a started task's entry function on this thread until it would block.
 *
 * The entry runs until it waits on an empty queue forever, spends its timed-wait budget, or
 * returns. Returns 0 when no such task is started.
 */
static inline int protocore_platform_host_task_run(protocore_platform_task t)
{
    int slot = protocore_task_slot(t);
    if (slot < 0 || !g_protocore_tasks.fn[slot])
    {
        return 0;
    }
    protocore_task_host_budget = PROTOCORE_TASK_HOST_WAITS;
    protocore_task_host_depth++;
    if (setjmp(protocore_task_host_jmp) == 0)
    {
        g_protocore_tasks.fn[slot](g_protocore_tasks.arg[slot]);
    }
    protocore_task_host_depth--;
    return 1;
}

/** @brief Run the started task that start was given @p name for. 0 when there is none. */
static inline int protocore_platform_host_task_run_named(const char *name)
{
    if (name == NULL)
    {
        return 0;
    }
    for (int i = 0; i < PROTOCORE_TASK_MAX; i++)
    {
        if (g_protocore_tasks.started[i] && g_protocore_tasks.name[i] && strcmp(g_protocore_tasks.name[i], name) == 0)
        {
            return protocore_platform_host_task_run(g_protocore_tasks.handle[i]);
        }
    }
    return 0;
}

/** @brief Run every started task's entry function once, lowest slot first. */
static inline int protocore_platform_host_tasks_run_all(void)
{
    int ran = 0;
    for (int i = 0; i < PROTOCORE_TASK_MAX; i++)
    {
        if (g_protocore_tasks.started[i] && g_protocore_tasks.fn[i])
        {
            ran += protocore_platform_host_task_run(g_protocore_tasks.handle[i]);
        }
    }
    return ran;
}

/** @brief Forget every started task, so one case does not inherit another's. */
static inline void protocore_platform_host_tasks_reset(void)
{
    memset(&g_protocore_tasks, 0, sizeof(g_protocore_tasks));
    protocore_platform_task_start_fail_once = 0;
}
static inline void protocore_platform_task_notify(protocore_platform_task t)
{
    (void)t;
}
static inline uint32_t protocore_platform_task_wait(int clear, uint32_t ticks)
{
    (void)clear;
    (void)ticks;
    // A timed wait returns on the target when its timeout expires, so it reports here too. Nothing
    // else runs to notify it, so a task looping on one spends its budget and unwinds.
    protocore_task_host_budget--;
    if (protocore_task_host_budget < 0)
    {
        protocore_task_host_yield();
    }
    return 0;
}
// Advances the virtual clock by the tick count. The host has no tick timer, so a wait that hands
// off here is what moves time forward; leaving this inert made any driver's pcdelay spin on a
// millisecond count that never changed. A test that drives the clock itself with set_millis is
// unaffected, since nothing calls this unless code under test is waiting.
static inline void protocore_platform_task_delay(uint32_t ticks)
{
    if (ticks)
    {
        set_millis(millis() + ticks);
    }
}
static inline void protocore_platform_task_yield_from_isr(int woke)
{
    (void)woke;
}
static inline protocore_platform_task protocore_platform_task_self(void)
{
    return (void *)1;
}

// ---------------------------------------------------------------------------
// Address
// ---------------------------------------------------------------------------

#define PROTOCORE_NET_TYPE_ANY 0
#define PROTOCORE_NET_TYPE_V4 4
#define PROTOCORE_NET_TYPE_V6 6

typedef struct
{
    uint8_t type;      // PROTOCORE_NET_TYPE_*
    uint8_t bytes[16]; // network order; v4 in the first 4
} protocore_net_ip;

// Behind a function rather than a file-scope object: this header reaches every translation unit,
// and a static variable most of them never name warns in each one.
static inline protocore_net_ip *protocore_net_host_any(void)
{
    static protocore_net_ip any;
    return &any;
}
#define PROTOCORE_NET_ADDR_ANY protocore_net_host_any()
#define PROTOCORE_NET_ADDR_ANY4 protocore_net_host_any()
#define PROTOCORE_NET_ADDR_ANY4_P protocore_net_host_any()

#define protocore_net_ip_is_v4(a) ((a) && (a)->type == PROTOCORE_NET_TYPE_V4)
#define protocore_net_ip_is_v6(a) ((a) && (a)->type == PROTOCORE_NET_TYPE_V6)
#define protocore_net_ip_as_v4(a) (a)
#define protocore_net_ip_as_v6(a) (a)

// The v6 address as bytes, and the v6 tag: lwIP keeps four network-order words, this keeps the
// sixteen bytes those words hold, so both answer the same question.
#define protocore_net_ip6_bytes(a) ((const uint8_t *)(a)->bytes)
#define protocore_net_ip6_wbytes(a) ((uint8_t *)(a)->bytes)
#define protocore_net_ip6_mark(a) ((a)->type = PROTOCORE_NET_TYPE_V6)

// The four octets as a word, the way lwIP's ip4_addr_get_u32 hands them back: the word's memory
// bytes are the address in network order. Composing the value arithmetically instead would byte
// reverse it on a little-endian host, and every caller here reads it as lwIP's.
static inline uint32_t protocore_net_ip4_u32(const protocore_net_ip *a)
{
    uint32_t v = 0;
    if (!a)
    {
        return 0;
    }
    memcpy(&v, a->bytes, 4);
    return v;
}
static inline void protocore_net_ip4_set(protocore_net_ip *a, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    if (!a)
    {
        return;
    }
    memset(a, 0, sizeof(*a));
    a->type = PROTOCORE_NET_TYPE_V4;
    a->bytes[0] = b0;
    a->bytes[1] = b1;
    a->bytes[2] = b2;
    a->bytes[3] = b3;
}
static inline int protocore_net_ip4_is_multicast(const protocore_net_ip *a)
{
    return a && a->type == PROTOCORE_NET_TYPE_V4 && (a->bytes[0] & 0xF0u) == 0xE0u;
}
// Dotted-quad only; the core's own RFC 4291 parser (network/ip.c) is what tests actually exercise.
static inline int protocore_net_ip_parse(const char *s, protocore_net_ip *out)
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
    protocore_net_ip4_set(out, (uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]);
    return 1;
}
static inline char *protocore_net_ip_print(const protocore_net_ip *a, char *buf, int cap)
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

#define PROTOCORE_NET_PBUF_TRANSPORT 0
#define PROTOCORE_NET_PBUF_RAM 0

typedef struct protocore_pbuf
{
    struct protocore_pbuf *next;
    void *payload;
    uint16_t tot_len;
    uint16_t len;
} protocore_pbuf;

// ---------------------------------------------------------------------------
// Result codes
// ---------------------------------------------------------------------------

typedef int8_t protocore_net_err;

#define PROTOCORE_NET_OK 0
#define PROTOCORE_NET_ERR_MEM (-1)
#define PROTOCORE_NET_ERR_BUF (-2)
#define PROTOCORE_NET_ERR_VAL (-6)
#define PROTOCORE_NET_ERR_ARG (-16)
#define PROTOCORE_NET_ERR_USE (-8)
#define PROTOCORE_NET_ERR_CONN (-11)
#define PROTOCORE_NET_ERR_CLSD (-15)
#define PROTOCORE_NET_ERR_RST (-14)
#define PROTOCORE_NET_ERR_ABRT (-13)

#define PROTOCORE_NET_WRITE_COPY 0x01
#define PROTOCORE_NET_OPT_REUSEADDR 0x04

// ---------------------------------------------------------------------------
// Control blocks
// ---------------------------------------------------------------------------

typedef struct protocore_pcb protocore_pcb;
typedef struct protocore_udp_pcb protocore_udp_pcb;

typedef protocore_net_err (*protocore_net_recv_fn)(void *, protocore_pcb *, protocore_pbuf *, protocore_net_err);
typedef protocore_net_err (*protocore_net_sent_fn)(void *, protocore_pcb *, uint16_t);
typedef protocore_net_err (*protocore_net_accept_fn)(void *, protocore_pcb *, protocore_net_err);
typedef protocore_net_err (*protocore_net_connect_fn)(void *, protocore_pcb *, protocore_net_err);
typedef void (*protocore_net_err_fn)(void *, protocore_net_err);
typedef void (*protocore_net_udp_recv_fn)(void *, protocore_udp_pcb *, protocore_pbuf *, const protocore_net_ip *,
                                          uint16_t);

struct protocore_pcb
{
    uint8_t tos;
    uint8_t ttl; // RFC 9293 sec 3.9.2 MUST-49: the TTL segments go out with, configurable
    uint8_t state;
    uint16_t local_port;
    uint16_t remote_port;
    protocore_net_ip local_ip;
    protocore_net_ip remote_ip;
    uint32_t so_options;
    uint16_t snd_queuelen; // segments still unacknowledged; the core waits for this to drain
    void *arg;
    protocore_net_recv_fn on_recv;
    protocore_net_sent_fn on_sent;
    protocore_net_accept_fn on_accept;
    protocore_net_err_fn on_err;
    int in_use;
};

struct protocore_udp_pcb
{
    uint8_t tos;
    uint16_t local_port;
    void *arg;
    protocore_net_udp_recv_fn on_recv;
    uint32_t so_options;
    int in_use;
};

// Fixed pools: no allocation, and a test can walk them to see what the core opened.
#ifndef PROTOCORE_NET_HOST_PCBS
#define PROTOCORE_NET_HOST_PCBS 16
#endif
PROTOCORE_HOST_SHARED protocore_pcb protocore_net_host_pcbs[PROTOCORE_NET_HOST_PCBS];
PROTOCORE_HOST_SHARED protocore_udp_pcb protocore_net_host_udp_pcbs[PROTOCORE_NET_HOST_PCBS];

/**
 * @brief A stable pcb a test can bind a slot to, when what it needs is only "this slot has one".
 *
 * Slot state that is set up by hand rather than by an accept still has to carry a non-null pcb,
 * because the core reads it to decide a connection is live. The last entry is reserved for that:
 * protocore_net_new() hands out from the front, so a test holding this one never collides with a pcb the
 * code under test allocated.
 */
static inline protocore_pcb *protocore_net_host_pcb(void)
{
    return &protocore_net_host_pcbs[PROTOCORE_NET_HOST_PCBS - 1];
}

// One-shot allocation failure: the next protocore_net_new() reports the control-block pool spent.
PROTOCORE_HOST_SHARED int protocore_net_host_new_fail_once;

static inline void mock_new_pcb_fail_once(void)
{
    protocore_net_host_new_fail_once = 1;
}

static inline protocore_pcb *protocore_net_new(int type)
{
    (void)type;
    if (protocore_net_host_new_fail_once)
    {
        protocore_net_host_new_fail_once = 0;
        return NULL;
    }
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        if (!protocore_net_host_pcbs[i].in_use)
        {
            memset(&protocore_net_host_pcbs[i], 0, sizeof(protocore_pcb));
            protocore_net_host_pcbs[i].in_use = 1;
            return &protocore_net_host_pcbs[i];
        }
    }
    return NULL;
}
// One-shot bind failure: the next bind reports the address already in use.
PROTOCORE_HOST_SHARED int protocore_net_host_bind_fail_once;

static inline void mock_bind_fail_once(void)
{
    protocore_net_host_bind_fail_once = 1;
}

static inline protocore_net_err protocore_net_bind(protocore_pcb *p, const protocore_net_ip *a, uint16_t port)
{
    (void)a;
    if (protocore_net_host_bind_fail_once)
    {
        protocore_net_host_bind_fail_once = 0;
        return PROTOCORE_NET_ERR_USE;
    }
    if (!p)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    p->local_port = port;
    return PROTOCORE_NET_OK;
}
// One-shot listen failure: the next listen reports no memory for the listen block.
PROTOCORE_HOST_SHARED int protocore_net_host_listen_fail_once;

static inline void mock_listen_fail_once(void)
{
    protocore_net_host_listen_fail_once = 1;
}

static inline protocore_pcb *protocore_net_listen(protocore_pcb *p, uint8_t backlog)
{
    (void)backlog;
    if (protocore_net_host_listen_fail_once)
    {
        protocore_net_host_listen_fail_once = 0;
        return NULL;
    }
    return p;
}
// One-shot connect refusal: the next protocore_net_connect() reports the peer unreachable and never
// completes, the way a RST to the SYN does.
PROTOCORE_HOST_SHARED int protocore_net_host_connect_fail_once;

static inline void mock_connect_fail_once(void)
{
    protocore_net_host_connect_fail_once = 1;
}

// The connect completes inline, the way protocore_net_call_marshal runs an op inline: the caller's
// callback fires before this returns, so a blocking open sees its flag on the first poll.
static inline protocore_net_err protocore_net_connect(protocore_pcb *p, const protocore_net_ip *a, uint16_t port,
                                                      protocore_net_connect_fn cb)
{
    if (!p)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    if (protocore_net_host_connect_fail_once)
    {
        protocore_net_host_connect_fail_once = 0;
        return PROTOCORE_NET_ERR_CONN;
    }
    if (a)
    {
        p->remote_ip = *a;
    }
    p->remote_port = port;
    if (cb)
    {
        cb(p->arg, p, PROTOCORE_NET_OK);
    }
    return PROTOCORE_NET_OK;
}
// One-shot close failure: the next protocore_net_close() reports no memory and leaves the pcb open, the
// way a stack that cannot queue the FIN does. The caller has to keep the slot draining, not drop it.
PROTOCORE_HOST_SHARED int protocore_net_host_close_fail_once;

static inline void mock_close_fail_once(void)
{
    protocore_net_host_close_fail_once = 1;
}

static inline protocore_net_err protocore_net_close(protocore_pcb *p)
{
    if (protocore_net_host_close_fail_once)
    {
        protocore_net_host_close_fail_once = 0;
        return PROTOCORE_NET_ERR_MEM;
    }
    if (p)
    {
        p->in_use = 0;
    }
    return PROTOCORE_NET_OK;
}
// How many aborts the code under test has issued. A slot reaped by an accept gate or a timeout
// sweep is only distinguishable from one closed cleanly by whether the stack was told to abort.
PROTOCORE_HOST_SHARED int protocore_net_host_abort_calls;

static inline int mock_abort_call_count(void)
{
    return protocore_net_host_abort_calls;
}

static inline void mock_abort_call_reset(void)
{
    protocore_net_host_abort_calls = 0;
}

static inline void protocore_net_abort(protocore_pcb *p)
{
    protocore_net_host_abort_calls++;
    if (p)
    {
        p->in_use = 0;
    }
}
static inline void protocore_net_arg(protocore_pcb *p, void *arg)
{
    if (p)
    {
        p->arg = arg;
    }
}
static inline void protocore_net_on_recv(protocore_pcb *p, protocore_net_recv_fn fn)
{
    if (p)
    {
        p->on_recv = fn;
    }
}
static inline void protocore_net_on_sent(protocore_pcb *p, protocore_net_sent_fn fn)
{
    if (p)
    {
        p->on_sent = fn;
    }
}
static inline void protocore_net_on_accept(protocore_pcb *p, protocore_net_accept_fn fn)
{
    if (p)
    {
        p->on_accept = fn;
    }
}
static inline void protocore_net_on_err(protocore_pcb *p, protocore_net_err_fn fn)
{
    if (p)
    {
        p->on_err = fn;
    }
}

// Sends are captured, not transmitted; protocore_net_host_sent() is what a test asserts on.
// Large enough to hold a multi-window file response whole: a test asserts on the body it sent, and
// a capture that stops at one window would report a truncation the transport never made.
#ifndef PROTOCORE_NET_HOST_TXCAP
#define PROTOCORE_NET_HOST_TXCAP 65536
#endif
PROTOCORE_HOST_SHARED uint8_t protocore_net_host_tx[PROTOCORE_NET_HOST_TXCAP];
PROTOCORE_HOST_SHARED size_t protocore_net_host_tx_len;

// After this many successful writes the next protocore_net_write reports a full send buffer and queues
// nothing, so a send pump takes its un-read-and-retry path. -1 never fails.
PROTOCORE_HOST_SHARED int protocore_net_host_write_fail_after = -1;

static inline void mock_send_fail_after(int n)
{
    protocore_net_host_write_fail_after = n;
}

static inline protocore_net_err protocore_net_write(protocore_pcb *p, const void *data, uint16_t len, uint8_t flags)
{
    (void)p;
    (void)flags;
    if (!data)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    if (protocore_net_host_write_fail_after == 0)
    {
        return PROTOCORE_NET_ERR_MEM;
    }
    if (protocore_net_host_write_fail_after > 0)
    {
        protocore_net_host_write_fail_after--;
    }
    if (protocore_net_host_tx_len + len > sizeof(protocore_net_host_tx))
    {
        return PROTOCORE_NET_ERR_MEM;
    }
    memcpy(protocore_net_host_tx + protocore_net_host_tx_len, data, len);
    protocore_net_host_tx_len += len;
    return PROTOCORE_NET_OK;
}
static inline protocore_net_err protocore_net_output(protocore_pcb *p)
{
    (void)p;
    return PROTOCORE_NET_OK;
}
// Window reopening, captured. On the target this is what moves the right window edge; RFC 9293
// section 3.8.6 makes the amount the assertable part (the receiver advertises RCV.BUFF - RCV.USER,
// and SHLD-14 says it never moves that edge left), so a no-op here left the transport's
// ack-on-consume model untestable. Records the call count, the running total, and the last length.
PROTOCORE_HOST_SHARED int protocore_net_host_recved_calls;
PROTOCORE_HOST_SHARED uint32_t protocore_net_host_recved_total;
PROTOCORE_HOST_SHARED uint16_t protocore_net_host_recved_last;

static inline void protocore_net_recved(protocore_pcb *p, uint16_t len)
{
    (void)p;
    protocore_net_host_recved_calls++;
    protocore_net_host_recved_total += len;
    protocore_net_host_recved_last = len;
}

/** @brief How many window updates the code under test issued. */
static inline int mock_recved_call_count(void)
{
    return protocore_net_host_recved_calls;
}
/** @brief Total bytes the window was reopened by since the last reset. */
static inline uint32_t mock_recved_total(void)
{
    return protocore_net_host_recved_total;
}
/** @brief The length the most recent window update carried. */
static inline uint16_t mock_recved_last(void)
{
    return protocore_net_host_recved_last;
}
static inline void mock_recved_reset(void)
{
    protocore_net_host_recved_calls = 0;
    protocore_net_host_recved_total = 0;
    protocore_net_host_recved_last = 0;
}
// How much room the stack reports for the next write. A test shrinks it to drive the
// backpressure-and-resume path, where a response has to page out across several worker loops
// instead of fitting one send.
#define MOCK_SNDBUF_DEFAULT 5744 /* a typical lwIP TCP_SND_BUF */
PROTOCORE_HOST_SHARED uint16_t protocore_net_host_sndbuf_val = MOCK_SNDBUF_DEFAULT;

static inline void mock_sndbuf_set(uint16_t v)
{
    protocore_net_host_sndbuf_val = v;
}

static inline uint16_t protocore_net_sndbuf(protocore_pcb *p)
{
    (void)p;
    return protocore_net_host_sndbuf_val;
}
static inline void protocore_net_nagle_disable(protocore_pcb *p)
{
    (void)p;
}
static inline void protocore_net_rcv_wnd_update(protocore_pcb *p, uint16_t len)
{
    (void)p;
    (void)len;
}
static inline void protocore_net_opt_set(void *p, uint32_t opt)
{
    (void)p;
    (void)opt;
}

// ---------------------------------------------------------------------------
// Packet buffers and the UDP send capture
// ---------------------------------------------------------------------------
//
// protocore_net_udp_sendto is where a datagram leaves the core, so it is the only place a test can see
// what the wire would have carried. It records the destination, the ports and the payload; the
// renderer in protocore_net_pcap.h turns that log into a .pcap the test parses and Wireshark opens.
//
// The log holds the fields rather than the pcap bytes because this header is parsed from inside
// protocore_config.h, before shared/protocore_types.h supplies PROTOCORE_INLINE - so pcap.h cannot be
// included here.

#ifndef PROTOCORE_NET_HOST_PBUFS
#define PROTOCORE_NET_HOST_PBUFS 8
#endif
#ifndef PROTOCORE_NET_HOST_DGRAM_LEN
#define PROTOCORE_NET_HOST_DGRAM_LEN 1472 // an Ethernet MTU less the IPv4 and UDP headers
#endif
#ifndef PROTOCORE_NET_HOST_DGRAMS
#define PROTOCORE_NET_HOST_DGRAMS 64
#endif

typedef struct
{
    protocore_pbuf p;
    uint8_t data[PROTOCORE_NET_HOST_DGRAM_LEN];
    int in_use;
} protocore_net_host_pbuf_slot;

PROTOCORE_HOST_SHARED protocore_net_host_pbuf_slot protocore_net_host_pbuf_pool[PROTOCORE_NET_HOST_PBUFS];
PROTOCORE_HOST_SHARED int protocore_net_host_pbuf_fail_once;

/** @brief One datagram the core handed to the stack. */
typedef struct
{
    uint8_t type;     // PROTOCORE_NET_TYPE_V4 / PROTOCORE_NET_TYPE_V6
    uint8_t addr[16]; // destination, network order; v4 in the first four
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t tos;
    uint32_t ms; // virtual-clock millisecond the send happened, the pcap record timestamp
    uint16_t len;
    uint8_t data[PROTOCORE_NET_HOST_DGRAM_LEN];
} protocore_net_host_dgram;

PROTOCORE_HOST_SHARED protocore_net_host_dgram protocore_net_host_dgrams[PROTOCORE_NET_HOST_DGRAMS];
PROTOCORE_HOST_SHARED size_t protocore_net_host_dgram_n;    // records kept
PROTOCORE_HOST_SHARED size_t protocore_net_host_dgram_sent; // sends seen, kept or not

// The next protocore_net_pbuf_alloc() reports the pool spent, so the send path's refuse branch is reachable.
static inline void mock_pbuf_fail_once(void)
{
    protocore_net_host_pbuf_fail_once = 1;
}

// A pbuf a test built on the stack is not in the pool, so it is left alone.
static inline void protocore_net_pbuf_free(protocore_pbuf *p)
{
    for (int i = 0; i < PROTOCORE_NET_HOST_PBUFS; i++)
    {
        if (&protocore_net_host_pbuf_pool[i].p == p)
        {
            protocore_net_host_pbuf_pool[i].in_use = 0;
            return;
        }
    }
}
static inline uint16_t protocore_net_pbuf_copy(const protocore_pbuf *p, void *dst, uint16_t len, uint16_t off)
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
static inline protocore_pbuf *protocore_net_pbuf_alloc(int layer, uint16_t len, int type)
{
    (void)layer;
    (void)type;
    if (protocore_net_host_pbuf_fail_once)
    {
        protocore_net_host_pbuf_fail_once = 0;
        return NULL;
    }
    if (len > PROTOCORE_NET_HOST_DGRAM_LEN)
    {
        return NULL;
    }
    for (int i = 0; i < PROTOCORE_NET_HOST_PBUFS; i++)
    {
        if (!protocore_net_host_pbuf_pool[i].in_use)
        {
            protocore_net_host_pbuf_pool[i].in_use = 1;
            protocore_net_host_pbuf_pool[i].p.next = NULL;
            protocore_net_host_pbuf_pool[i].p.payload = protocore_net_host_pbuf_pool[i].data;
            protocore_net_host_pbuf_pool[i].p.len = len;
            protocore_net_host_pbuf_pool[i].p.tot_len = len;
            return &protocore_net_host_pbuf_pool[i].p;
        }
    }
    return NULL;
}

// First member of the core's call record; fn casts back to that record. lwIP puts a semaphore here.
typedef struct protocore_net_call
{
    int sem;
} protocore_net_call;

static inline protocore_net_err protocore_net_call_marshal(protocore_net_err (*fn)(protocore_net_call *),
                                                           protocore_net_call *c)
{
    return fn ? fn(c) : PROTOCORE_NET_OK;
}

static inline protocore_udp_pcb *protocore_net_udp_new(void)
{
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        if (!protocore_net_host_udp_pcbs[i].in_use)
        {
            memset(&protocore_net_host_udp_pcbs[i], 0, sizeof(protocore_udp_pcb));
            protocore_net_host_udp_pcbs[i].in_use = 1;
            return &protocore_net_host_udp_pcbs[i];
        }
    }
    return NULL;
}
static inline protocore_net_err protocore_net_udp_bind(protocore_udp_pcb *p, const protocore_net_ip *a, uint16_t port)
{
    (void)a;
    if (!p)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    p->local_port = port;
    return PROTOCORE_NET_OK;
}
static inline void protocore_net_udp_recv(protocore_udp_pcb *p, protocore_net_udp_recv_fn fn, void *arg)
{
    if (p)
    {
        p->on_recv = fn;
        p->arg = arg;
    }
}
// After this many successful datagrams the next protocore_net_udp_sendto refuses and records nothing, the
// way a stack with no route to the destination does. -1 never fails.
PROTOCORE_HOST_SHARED int protocore_net_host_udp_fail_after = -1;

static inline void mock_udp_send_fail_after(int n)
{
    protocore_net_host_udp_fail_after = n;
}

// Record the datagram. The count keeps rising past the log so a test can tell "sent more than the
// log holds" from "stopped sending".
static inline protocore_net_err protocore_net_udp_sendto(protocore_udp_pcb *p, protocore_pbuf *b,
                                                         const protocore_net_ip *a, uint16_t port)
{
    if (!p || !b || !a)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    if (protocore_net_host_udp_fail_after == 0)
    {
        return PROTOCORE_NET_ERR_RST;
    }
    if (protocore_net_host_udp_fail_after > 0)
    {
        protocore_net_host_udp_fail_after--;
    }
    protocore_net_host_dgram_sent++;
    if (protocore_net_host_dgram_n < PROTOCORE_NET_HOST_DGRAMS)
    {
        protocore_net_host_dgram *d = &protocore_net_host_dgrams[protocore_net_host_dgram_n];
        memset(d, 0, sizeof(*d));
        d->type = a->type;
        memcpy(d->addr, a->bytes, sizeof(d->addr));
        d->src_port = p->local_port;
        d->dst_port = port;
        d->tos = p->tos;
        d->ms = (uint32_t)millis();
        d->len = protocore_net_pbuf_copy(b, d->data, (uint16_t)sizeof(d->data), 0);
        protocore_net_host_dgram_n++;
    }
    return PROTOCORE_NET_OK;
}
static inline void protocore_net_udp_remove(protocore_udp_pcb *p)
{
    if (p)
    {
        p->in_use = 0;
    }
}
#define PROTOCORE_NET_HAS_IGMP 1
#define PROTOCORE_NET_HAS_IPV6 1

static inline protocore_net_err protocore_net_igmp_join(const protocore_net_ip *nif, const protocore_net_ip *grp)
{
    (void)nif;
    (void)grp;
    return PROTOCORE_NET_OK;
}
static inline protocore_net_err protocore_net_igmp_leave(const protocore_net_ip *nif, const protocore_net_ip *grp)
{
    (void)nif;
    (void)grp;
    return PROTOCORE_NET_OK;
}

// ---------------------------------------------------------------------------
// Test entry points
// ---------------------------------------------------------------------------

/** @brief Bytes the core has written since the last reset, and how many. */
static inline const uint8_t *protocore_net_host_sent(size_t *len)
{
    if (len)
    {
        *len = protocore_net_host_tx_len;
    }
    return protocore_net_host_tx;
}

/** @brief Datagrams held in the log. */
static inline size_t protocore_net_host_udp_count(void)
{
    return protocore_net_host_dgram_n;
}

/** @brief Datagrams the core handed to the stack, whether or not the log had room. */
static inline size_t protocore_net_host_udp_sent(void)
{
    return protocore_net_host_dgram_sent;
}

/** @brief Datagram @p i, or NULL past the end of the log. */
static inline const protocore_net_host_dgram *protocore_net_host_udp_at(size_t i)
{
    if (i >= protocore_net_host_dgram_n)
    {
        return NULL;
    }
    return &protocore_net_host_dgrams[i];
}

/** @brief Drop the datagram log and release every pbuf. */
static inline void protocore_net_host_udp_reset(void)
{
    protocore_net_host_dgram_n = 0;
    protocore_net_host_dgram_sent = 0;
    protocore_net_host_pbuf_fail_once = 0;
    protocore_net_host_udp_fail_after = -1;
    memset(protocore_net_host_pbuf_pool, 0, sizeof(protocore_net_host_pbuf_pool));
}

/** @brief Drop the capture and every pcb, so each test starts from a known state. */
static inline void protocore_net_host_reset(void)
{
    protocore_net_host_tx_len = 0;
    memset(protocore_net_host_pcbs, 0, sizeof(protocore_net_host_pcbs));
    memset(protocore_net_host_udp_pcbs, 0, sizeof(protocore_net_host_udp_pcbs));
    protocore_net_host_udp_reset();
}

// The same capture read as text. A response is a string for most of the suite - it asserts on a
// status line or a header - so this NUL-terminates what was written and hands it back. The write
// path bounds the length against the buffer, so there is always a byte left for the terminator.
static inline void tcp_capture_reset(void)
{
    protocore_net_host_tx_len = 0;
    protocore_net_host_tx[0] = '\0';
    protocore_net_host_write_fail_after = -1; // clear a send failure a prior test armed
}

// The host always captures, so there is no capture to switch off. What a caller wants here is to
// stop collecting and then read what was collected, so the buffer is left intact.
static inline void tcp_capture_disable(void)
{
}

static inline const char *tcp_captured(void)
{
    size_t n = protocore_net_host_tx_len < sizeof(protocore_net_host_tx) ? protocore_net_host_tx_len
                                                                         : sizeof(protocore_net_host_tx) - 1;
    protocore_net_host_tx[n] = '\0';
    return (const char *)protocore_net_host_tx;
}

static inline size_t tcp_captured_len(void)
{
    return protocore_net_host_tx_len;
}

/** @brief Deliver @p n bytes to @p p's recv callback as one segment. */
static inline protocore_net_err protocore_net_host_deliver(protocore_pcb *p, void *data, uint16_t n)
{
    if (!p || !p->on_recv)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    protocore_pbuf b;
    memset(&b, 0, sizeof(b));
    b.payload = data;
    b.len = n;
    b.tot_len = n;
    return p->on_recv(p->arg, p, &b, PROTOCORE_NET_OK);
}

/** @brief Deliver a peer FIN (a null pbuf) to @p p's recv callback. */
static inline protocore_net_err protocore_net_host_close_peer(protocore_pcb *p)
{
    if (!p || !p->on_recv)
    {
        return PROTOCORE_NET_ERR_ARG;
    }
    return p->on_recv(p->arg, p, NULL, PROTOCORE_NET_OK);
}

/** @brief The UDP pcb bound to @p port, or NULL when nothing bound it. */
static inline protocore_udp_pcb *protocore_net_host_udp_pcb(uint16_t port)
{
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        if (protocore_net_host_udp_pcbs[i].in_use && protocore_net_host_udp_pcbs[i].local_port == port)
        {
            return &protocore_net_host_udp_pcbs[i];
        }
    }
    return NULL;
}

/**
 * @brief Deliver @p n bytes to the pcb bound to @p port, as one datagram from @p src_ip:@p src_port.
 *
 * Calls the recv callback the core armed, which is the stack's own producer, so what runs is the
 * receive path the target runs. Returns 0 when no pcb is bound to that port. The payload reaches
 * the handler on the next poll(), which drains the ring the callback filled.
 */
static inline int protocore_net_host_udp_deliver(uint16_t port, const char *src_ip, uint16_t src_port, void *data,
                                                 uint16_t n)
{
    protocore_udp_pcb *p = protocore_net_host_udp_pcb(port);
    if (!p || !p->on_recv)
    {
        return 0;
    }
    protocore_net_ip src;
    memset(&src, 0, sizeof(src));
    if (src_ip)
    {
        protocore_net_ip_parse(src_ip, &src);
    }
    protocore_pbuf b;
    memset(&b, 0, sizeof(b));
    b.payload = data;
    b.len = n;
    b.tot_len = n;
    p->on_recv(p->arg, p, &b, &src, src_port);
    return 1;
}

#endif // PROTOCORE_PROTOCORE_NET_HOST_H
