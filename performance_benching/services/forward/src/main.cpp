// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the interface forwarding plane (services/net/forward), the v5
// bridge / router. pc_forward_ingress() is the whole hot path: it runs the ingress ACL (fixed-
// offset byte pattern under a mask), the optional inspection hook, the policy routes, then the
// src->dst rule resolution (a DENY wins over an ALLOW, default-deny otherwise) and the per-rule
// rate cap, before handing the bytes to each destination's egress send callback. Everything above
// that callback is pure - static tables, zero heap - so every call here exercises the real
// production decision code.
//
// Deliberately out of scope: the egress itself. The plane is decoupled from its transports (the
// canonical wiring is DMA-complete -> FORWARD lane -> ingress -> egress DMA), and this rig has no
// NIC / radio / bus attached, so the send callbacks are stubs exactly as the host test stubs them
// (test/test_forward/test_forward.cpp's cap_send): a tiny function that accepts the bytes and
// returns true, never a real transaction. sink_send() only accumulates a byte count; stage_send()
// additionally memcpy's the frame into a static staging buffer, standing in for the egress
// DMA-descriptor staging a real interface would do - that one is used for the MTU-sized bulk
// figure so the MB/s number reflects a frame actually being moved, not just a pointer decision.
// The rate cap is left unlimited (cap 0) on every benched path: on device the window is driven by
// pc_millis() (pc_forward_test_set_now() is host-only), so a cap would start dropping frames
// mid-measurement and time the drop path instead of the forward path.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/forward -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/net/forward/forward.h"
#include <Arduino.h>

// --- egress stubs (no hardware; same shape as the host test's cap_send) ----------------------
static volatile uint32_t g_egress_bytes = 0;

// Accepts and counts. Used wherever the decision cost, not the copy cost, is what is timed.
static bool sink_send(uint8_t, const uint8_t *, uint16_t len, void *)
{
    g_egress_bytes += len;
    return true;
}

static uint8_t g_stage[1600];

// Accepts and stages the frame (stand-in for handing bytes to an egress DMA descriptor).
static bool stage_send(uint8_t, const uint8_t *d, uint16_t len, void *)
{
    memcpy(g_stage, d, (len > sizeof(g_stage)) ? sizeof(g_stage) : len);
    return true;
}

#if PC_FWD_INSPECT
// Realistic inspector: peek the EtherType and drop IPv6 (0x86DD), pass everything else.
static pc_fwd_verdict inspect_ethertype(uint8_t, const uint8_t *d, uint16_t n, void *)
{
    if (n >= 14 && d[12] == 0x86 && d[13] == 0xDD)
    {
        return pc_fwd_verdict::PC_FWD_INSPECT_DROP;
    }
    return pc_fwd_verdict::PC_FWD_INSPECT_PASS;
}
#endif

// A 64-byte Ethernet-II / IPv4 / UDP frame: dst+src MAC, EtherType 0x0800 at offset 12 (the field
// the ACL and the policy route below key on), a 20-byte IPv4 header, an 8-byte UDP header, payload.
static const uint8_t frame64[64] = {
    0x02, 0x00, 0x00, 0x00, 0x00, 0x02,                         // dst MAC
    0x02, 0x00, 0x00, 0x00, 0x00, 0x01,                         // src MAC
    0x08, 0x00,                                                 // EtherType = IPv4
    0x45, 0x00, 0x00, 0x32, 0x00, 0x01, 0x00, 0x00, 0x40, 0x11, // IPv4 hdr
    0x00, 0x00, 0xC0, 0xA8, 0x01, 0x0A, 0xC0, 0xA8, 0x01, 0x14, // .. 192.168.1.10 -> .20
    0x13, 0x88, 0x13, 0x89, 0x00, 0x1E, 0x00, 0x00,             // UDP 5000 -> 5001, len 30
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, // payload
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

static uint8_t frame1500[1500]; // MTU-sized frame, same 14-byte L2 header

// EtherType IPv4 at offset 12, matched exactly - the ACL / policy-route pattern primitive.
static const uint8_t pat_ipv4[2] = {0x08, 0x00};
static const uint8_t msk_ff[2] = {0xFF, 0xFF};

// Register interfaces 1..n_if with `send` (ids are what the rules/routes below refer to).
static void add_ifaces(uint8_t n_if, pc_if_send_fn send)
{
    for (uint8_t id = 1; id <= n_if; id++)
    {
        pc_forward_add_if(id, pc_if_kind::PC_IF_ETH, send, nullptr);
    }
}

static void forward_bench_task(void *)
{
    memcpy(frame1500, frame64, 14); // L2 header, then a deterministic payload
    for (size_t i = 14; i < sizeof(frame1500); i++)
    {
        frame1500[i] = (uint8_t)(i * 31u);
    }

    volatile uint32_t sink = 0;

    for (;;)
    {
        Serial.printf("DB ==== forward device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        // 1) The bridge hot path: an ALLOW rule to each of two destinations, so one ingress frame
        //    fans out to both (never reflected to the source).
        pc_forward_reset();
        add_ifaces(3, sink_send);
        pc_forward_add_rule(1, 2, pc_fwd_action::PC_FWD_ALLOW, 0);
        pc_forward_add_rule(1, 3, pc_fwd_action::PC_FWD_ALLOW, 0);
        DBENCH_OP("fwd ingress allow fanout x2", 20000, sink += pc_forward_ingress(1, frame64, sizeof(frame64)));

        // 2) Default-deny: interfaces registered but no rule matches - pure rule-resolution cost.
        pc_forward_reset();
        add_ifaces(3, sink_send);
        DBENCH_OP("fwd ingress default-deny", 50000, sink += pc_forward_ingress(1, frame64, sizeof(frame64)));

        // 3) Ingress ACL: deny by EtherType at offset 12 (masked byte-pattern match), frame dropped
        //    before any forwarding rule runs.
        pc_forward_reset();
        add_ifaces(2, sink_send);
        pc_forward_add_rule(1, 2, pc_fwd_action::PC_FWD_ALLOW, 0);
        pc_forward_acl_add(1, 12, pat_ipv4, msk_ff, 2, pc_fwd_action::PC_FWD_DENY);
        DBENCH_OP("fwd ingress acl deny", 50000, sink += pc_forward_ingress(1, frame64, sizeof(frame64)));

        // 4) Policy route: the same pattern primitive binds IPv4 traffic to egress if 3, taking
        //    precedence over the 1->2 fan-out rule.
        pc_forward_reset();
        add_ifaces(3, sink_send);
        pc_forward_add_rule(1, 2, pc_fwd_action::PC_FWD_ALLOW, 0);
        pc_forward_route_add(PC_FWD_IF_ANY, 12, pat_ipv4, msk_ff, 2, 3, 0);
        DBENCH_OP("fwd ingress policy-routed", 20000, sink += pc_forward_ingress(1, frame64, sizeof(frame64)));

#if PC_FWD_INSPECT
        // 5) Inspection hook installed: ACL -> inspector (passes this frame) -> forward.
        pc_forward_reset();
        add_ifaces(2, sink_send);
        pc_forward_add_rule(1, 2, pc_fwd_action::PC_FWD_ALLOW, 0);
        pc_forward_set_inspector(inspect_ethertype, nullptr);
        DBENCH_OP("fwd ingress + inspector", 20000, sink += pc_forward_ingress(1, frame64, sizeof(frame64)));
#endif

        // 6) MTU-sized frame across the plane with a staging egress - decision + one frame copy,
        //    i.e. the throughput a single-destination bridge hop can sustain.
        pc_forward_reset();
        add_ifaces(2, stage_send);
        pc_forward_add_rule(1, 2, pc_fwd_action::PC_FWD_ALLOW, 0);
        DBENCH_BULK("fwd ingress 1500B -> egress", 5000, sizeof(frame1500),
                    sink += pc_forward_ingress(1, frame1500, (uint16_t)sizeof(frame1500)));

        (void)sink;
        (void)g_egress_bytes;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: forward device microbench");
    xTaskCreatePinnedToCore(forward_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
