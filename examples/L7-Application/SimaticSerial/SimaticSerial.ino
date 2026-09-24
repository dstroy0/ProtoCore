// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SimaticSerial.ino
 * @brief Siemens SIMATIC serial (3964R + RK512) - two link stations talking to each other on one device,
 *        the full interactive handshake + an RK512 FETCH round-trip, surfaced over HTTP (PROTOCORE_ENABLE_SIMATIC).
 *
 * services/fieldbus/simatic is a pure 3964R link + RK512 telegram codec; the RS-232 / RS-485 UART is normally the
 * application's. To show the whole protocol self-contained (no second device, no wiring), this sketch runs
 * TWO link contexts - station A (high priority) and station B (low priority) - and cross-wires their byte
 * sinks through small queues, so A's transmit feeds B's receive and vice versa. Each loop:
 *
 *   A sends an RK512 FETCH (read 2 words from DB5) framed by 3964R  ->  the STX/DLE handshake carries it to
 *   B  ->  B decodes the FETCH and replies with an RK512 reaction (status ok)  ->  the handshake carries it
 *   back to A. The last exchange is served as JSON at GET /simatic.
 *
 * On a real installation A would be this device and B a Siemens PtP CP over an RS-232/RS-485 UART; here both
 * live on-device to demonstrate + self-test the codec on the hardware.
 *
 * Build flags (platformio.ini):  build_flags = -DPROTOCORE_ENABLE_SIMATIC=1
 */

#define PROTOCORE_ENABLE_SIMATIC 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/fieldbus/simatic/simatic.h"
#include "server/clock/clock.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// A tiny byte queue to cross-wire the two stations without re-entrancy (a tx sink enqueues; loop() drains
// into the peer's rx).
struct ByteQ
{
    uint8_t buf[128];
    size_t head, tail;
};
static ByteQ q_a2b, q_b2a;
static void q_push(ByteQ *q, uint8_t b)
{
    size_t n = (q->head + 1) % sizeof(q->buf);
    if (n != q->tail)
    {
        q->buf[q->head] = b;
        q->head = n;
    }
}
static bool q_pop(ByteQ *q, uint8_t *b)
{
    if (q->head == q->tail)
    {
        return false;
    }
    *b = q->buf[q->tail];
    q->tail = (q->tail + 1) % sizeof(q->buf);
    return true;
}

static Simatic3964Ctx sta_a, sta_b;

static uint32_t now_ms()
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

// Thin wrappers over the Simatic entries: set the args a call takes, run it, read the outcome.
static void link_send(Simatic3964Ctx *ctx, const uint8_t *data, size_t len, uint32_t now)
{
    SimaticV.send_3964r_args.ctx = ctx;
    SimaticV.send_3964r_args.data = data;
    SimaticV.send_3964r_args.len = len;
    SimaticV.send_3964r_args.now_ms = now;
    Simatic.send_3964r(protocore_simatic_span());
}

static void link_rx_byte(Simatic3964Ctx *ctx, uint8_t b, uint32_t now)
{
    SimaticV.rx_byte_3964r_args.ctx = ctx;
    SimaticV.rx_byte_3964r_args.b = b;
    SimaticV.rx_byte_3964r_args.now_ms = now;
    Simatic.rx_byte_3964r(protocore_simatic_span());
}

static void link_tick(Simatic3964Ctx *ctx, uint32_t now)
{
    SimaticV.tick_3964r_args.ctx = ctx;
    SimaticV.tick_3964r_args.now_ms = now;
    Simatic.tick_3964r(protocore_simatic_span());
}

static bool link_idle(const Simatic3964Ctx *ctx)
{
    SimaticV.idle_3964r_args.ctx = ctx;
    Simatic.idle_3964r(protocore_simatic_span());
    return SimaticV.ok;
}

static void link_init(Simatic3964Ctx *ctx, bool high_priority, bool with_bcc, Simatic3964TxFn tx,
                      Simatic3964RxFn rx, void *user)
{
    SimaticV.init_3964r_args.ctx = ctx;
    SimaticV.init_3964r_args.high_priority = high_priority;
    SimaticV.init_3964r_args.with_bcc = with_bcc;
    SimaticV.init_3964r_args.tx = tx;
    SimaticV.init_3964r_args.rx = rx;
    SimaticV.init_3964r_args.user = user;
    Simatic.init_3964r(protocore_simatic_span());
}

// Station A's transmit goes to B; B's transmit goes to A.
static void a_tx(void *u, uint8_t b)
{
    (void)u;
    q_push(&q_a2b, b);
}
static void b_tx(void *u, uint8_t b)
{
    (void)u;
    q_push(&q_b2a, b);
}

// Last exchange, for the HTTP view.
static volatile uint32_t g_round = 0;
static char g_last_fetch[48] = "(none)";
static char g_last_reaction[48] = "(none)";

static void hexstr(char *out, size_t cap, const uint8_t *d, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 3 < cap; i++)
    {
        o += (size_t)snprintf(out + o, cap - o, "%02x", d[i]);
    }
    out[o] = '\0';
}

// B received a telegram from A: decode the FETCH, reply with an RK512 reaction (status ok).
static void b_on_rx(void *u, const uint8_t *d, size_t n)
{
    (void)u;
    hexstr(g_last_fetch, sizeof(g_last_fetch), d, n);
    Rk512Header h;
    SimaticV.parse_header_rk512_args.buf = d;
    SimaticV.parse_header_rk512_args.len = n;
    SimaticV.parse_header_rk512_args.out = &h;
    Simatic.parse_header_rk512(protocore_simatic_span());
    if (SimaticV.ok && h.cmd == RK512_CMD_FETCH)
    {
        uint8_t react[8];
        SimaticV.build_reaction_rk512_args.buf = react;
        SimaticV.build_reaction_rk512_args.cap = sizeof(react);
        SimaticV.build_reaction_rk512_args.status = 0x0000; // ok
        Simatic.build_reaction_rk512(protocore_simatic_span());
        size_t rn = SimaticV.n;
        link_send(&sta_b, react, rn, now_ms());
    }
}

// A received B's reaction: record it and count the completed round.
static void a_on_rx(void *u, const uint8_t *d, size_t n)
{
    (void)u;
    hexstr(g_last_reaction, sizeof(g_last_reaction), d, n);
    uint16_t status = 0xFFFF;
    SimaticV.parse_reaction_rk512_args.buf = d;
    SimaticV.parse_reaction_rk512_args.len = n;
    SimaticV.parse_reaction_rk512_args.status = &status;
    SimaticV.parse_reaction_rk512_args.data = nullptr;
    SimaticV.parse_reaction_rk512_args.dlen = nullptr;
    Simatic.parse_reaction_rk512(protocore_simatic_span());
    if (SimaticV.ok && status == 0)
    {
        g_round++;
    }
}

static void handle_status(uint8_t slot, HttpReq *req)
{
    (void)req;
    char body[192];
    int nn =
        snprintf(body, sizeof(body), "{\"rounds\":%lu,\"lastFetchTelegram\":\"%s\",\"lastReactionTelegram\":\"%s\"}",
                 (unsigned long)g_round, g_last_fetch, g_last_reaction);
    send_bin(slot, 200, "application/json", (const uint8_t *)body, (size_t)(nn < 0 ? 0 : nn));
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("SIMATIC 3964R/RK512 demo at http://%u.%u.%u.%u/simatic\n", (unsigned)(ip & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Both stations run the "R" (BCC) variant; A is high priority, B low (collision arbitration).
    link_init(&sta_a, /*high_priority=*/true, /*with_bcc=*/true, a_tx, a_on_rx, nullptr);
    link_init(&sta_b, /*high_priority=*/false, /*with_bcc=*/true, b_tx, b_on_rx, nullptr);

    on_http("/simatic", HTTP_GET, handle_status);
    begin_http(80, NULL);
}

void loop()
{
    handle();
    uint32_t now = now_ms();

    // Pump the cross-wire: bytes A sent -> B's receiver, bytes B sent -> A's receiver.
    uint8_t b;
    while (q_pop(&q_a2b, &b))
    {
        link_rx_byte(&sta_b, b, now);
    }
    while (q_pop(&q_b2a, &b))
    {
        link_rx_byte(&sta_a, b, now);
    }
    link_tick(&sta_a, now);
    link_tick(&sta_b, now);

    // Kick off a new FETCH from A once both stations are idle (previous exchange finished).
    static uint32_t last = 0;
    if (link_idle(&sta_a) && link_idle(&sta_b) && now - last >= 1000)
    {
        last = now;
        uint8_t fetch[8];
        SimaticV.build_fetch_rk512_args.buf = fetch;
        SimaticV.build_fetch_rk512_args.cap = sizeof(fetch);
        SimaticV.build_fetch_rk512_args.area = RK512_AREA_DB;
        SimaticV.build_fetch_rk512_args.dbnr = 5;
        SimaticV.build_fetch_rk512_args.addr = 0x0000;
        SimaticV.build_fetch_rk512_args.wcount = 2;
        Simatic.build_fetch_rk512(protocore_simatic_span());
        size_t fn = SimaticV.n;
        link_send(&sta_a, fetch, fn, now);
    }
}
