// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SimaticSerial.ino
 * @brief Siemens SIMATIC serial (3964R + RK512) - two link stations talking to each other on one device,
 *        the full interactive handshake + an RK512 FETCH round-trip, surfaced over HTTP (PC_ENABLE_SIMATIC).
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
 * Build flags (platformio.ini):  build_flags = -DPC_ENABLE_SIMATIC=1
 */

#define PC_ENABLE_SIMATIC 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
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
    if (pc_rk512_parse_header(d, n, &h) && h.cmd == Rk512Cmd::FETCH)
    {
        uint8_t react[8];
        size_t rn = pc_rk512_build_reaction(react, sizeof(react), 0x0000); // ok
        pc_3964r_send(&sta_b, react, rn, pc_millis());
    }
}

// A received B's reaction: record it and count the completed round.
static void a_on_rx(void *u, const uint8_t *d, size_t n)
{
    (void)u;
    hexstr(g_last_reaction, sizeof(g_last_reaction), d, n);
    uint16_t status = 0xFFFF;
    if (pc_rk512_parse_reaction(d, n, &status, nullptr, nullptr) && status == 0)
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
    send_text(slot, 200, "application/json", (const uint8_t *)body, (size_t)(nn < 0 ? 0 : nn));
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
    {
        delay(250);
    }
    uint32_t ip = Physical.link->egress_ip();
    Serial.printf("SIMATIC 3964R/RK512 demo at http://%u.%u.%u.%u/simatic\n", (unsigned)(ip & 0xFF),
                  (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Both stations run the "R" (BCC) variant; A is high priority, B low (collision arbitration).
    pc_3964r_init(&sta_a, /*high_priority=*/true, /*with_bcc=*/true, a_tx, a_on_rx, nullptr);
    pc_3964r_init(&sta_b, /*high_priority=*/false, /*with_bcc=*/true, b_tx, b_on_rx, nullptr);

    on_http("/simatic", HTTP_GET, handle_status);
    begin_http(80, NULL);
}

void loop()
{
    handle();
    uint32_t now = pc_millis();

    // Pump the cross-wire: bytes A sent -> B's receiver, bytes B sent -> A's receiver.
    uint8_t b;
    while (q_pop(&q_a2b, &b))
    {
        pc_3964r_rx_byte(&sta_b, b, now);
    }
    while (q_pop(&q_b2a, &b))
    {
        pc_3964r_rx_byte(&sta_a, b, now);
    }
    pc_3964r_tick(&sta_a, now);
    pc_3964r_tick(&sta_b, now);

    // Kick off a new FETCH from A once both stations are idle (previous exchange finished).
    static uint32_t last = 0;
    if (pc_3964r_idle(&sta_a) && pc_3964r_idle(&sta_b) && now - last >= 1000)
    {
        last = now;
        uint8_t fetch[8];
        size_t fn = pc_rk512_build_fetch(fetch, sizeof(fetch), Rk512Area::DB, 5, 0x0000, 2);
        pc_3964r_send(&sta_a, fetch, fn, now);
    }
}
