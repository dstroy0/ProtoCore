// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Ptp.ino
 * @brief PTP / IEEE 1588 ordinary clock - grandmaster OR slave (PC_ENABLE_PTP).
 *
 * The Precision Time Protocol keeps a LAN's clocks in lock-step. Set PTP_MASTER below:
 *
 *  - **PTP_MASTER 1 (grandmaster):** the device *sources* time and hands it out. Each second it sends
 *    Announce + Sync + Follow_Up (with its precise send time), and answers each slave's Delay_Req with
 *    a Delay_Resp. Feed `master_time_ns()` from an accurate source - a GPS fix (see the UbloxGnss
 *    example), a DS3231/PCF8523 RTC, `pc_ntp_epoch()`, or the Time Source feature - and every slave
 *    on the LAN follows it. The Announce advertises clockClass 6 (locked to a primary reference) and
 *    timeSource GPS.
 *  - **PTP_MASTER 0 (slave):** the device follows a master, running the four-message exchange
 *    (Sync/Follow_Up/Delay_Req/Delay_Resp) and reporting offset + path delay.
 *
 * Timestamps here are taken in software (in the UDP callback / send path) with `pc_micros()`, so the
 * accuracy is millisecond-class over Wi-Fi. Sub-microsecond PTP needs MAC hardware timestamping, which
 * this chip's Ethernet MAC supports but Wi-Fi does not - so on Ethernet this same code gets far tighter.
 *
 * Build flag (PlatformIO): `-DPC_ENABLE_PTP=1`
 */

#define PC_ENABLE_PTP 1
#define PTP_MASTER 0 // 1 = grandmaster (source time from GPS/RTC/...), 0 = slave (follow a master)

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/transport/udp.h"
#include "server/clock/clock.h"
#include "network_drivers/application/ptp/ptp.h"


static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const char *PTP_GROUP = "224.0.1.129"; // PTP default (primary) domain multicast address
static pc_ip g_group;                   // PTP_GROUP, parsed once in setup()

// This clock's port identity (a locally-administered EUI-64 clockIdentity + port 1).
static uint8_t my_clock_id[8] = {0x02, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x01};
static const uint16_t MY_PORT = 1;

static int64_t now_ns()
{
    return (int64_t)pc_micros() * 1000;
} // local monotonic software timestamp

// ------- accurate time source (grandmaster) -------
// Wire this to your reference: a GPS fix (UbloxGnss), a DS3231 RTC, pc_ntp_epoch(), or the Time
// Source feature. Standalone, it advances a base epoch with the monotonic clock. Set s_base_ns from
// your source (e.g. on each GPS fix: s_base_ns = gps_epoch_ns - now_ns();).
static int64_t s_base_ns = 0;
static int64_t master_time_ns()
{
    return s_base_ns + now_ns();
}

// %lld is not always available in newlib-nano; format int64 by hand.
static char *i64(int64_t v, char *buf)
{
    char tmp[24];
    int i = 0;
    bool neg = v < 0;
    uint64_t u = neg ? ~(uint64_t)v + 1u : (uint64_t)v;
    if (u == 0)
    {
        tmp[i++] = '0';
    }
    while (u)
    {
        tmp[i++] = (char)('0' + (int)(u % 10));
        u /= 10;
    }
    int j = 0;
    if (neg)
    {
        buf[j++] = '-';
    }
    while (i)
    {
        buf[j++] = tmp[--i];
    }
    buf[j] = 0;
    return buf;
}

#if PTP_MASTER

static uint16_t m_seq = 0;

static void master_tick()
{
    pc_ptp_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.clock_identity, my_clock_id, 8);
    h.port_number = MY_PORT;
    h.sequence_id = m_seq;
    uint8_t buf[80];

    // Announce: advertise this grandmaster's quality + identity.
    pc_ptp_announce a;
    memset(&a, 0, sizeof(a));
    a.utc_offset = 37;    // TAI - UTC as of 2017+
    a.gm_priority1 = 128; // default selectable priority
    a.gm_priority2 = 128;
    a.gm_clock_class = 6;       // 6 = locked to a primary reference (GPS); 248 = free-running default
    a.gm_clock_accuracy = 0x21; // within ~100 ns
    a.gm_variance = 0xFFFF;
    memcpy(a.gm_identity, my_clock_id, 8);
    a.steps_removed = 0;
    a.time_source = 0x20; // GPS
    pc_ptp_ts_from_ns(master_time_ns(), &a.origin);
    size_t n = pc_ptp_build_announce(buf, sizeof(buf), &h, &a);
    Udp.listener->sendto(PC_PTP_GENERAL_PORT, &g_group, PC_PTP_GENERAL_PORT, buf, n);

    // Sync (two-step: the precise time follows in Follow_Up).
    h.flags = 0x0200; // twoStepFlag
    pc_ptp_timestamp zero = {0, 0};
    n = pc_ptp_build_sync(buf, sizeof(buf), &h, &zero);
    int64_t t1 = master_time_ns(); // precise egress instant
    Udp.listener->sendto(PC_PTP_EVENT_PORT, &g_group, PC_PTP_EVENT_PORT, buf, n);

    // Follow_Up carries t1.
    h.flags = 0;
    pc_ptp_timestamp ts1;
    pc_ptp_ts_from_ns(t1, &ts1);
    n = pc_ptp_build_follow_up(buf, sizeof(buf), &h, &ts1);
    Udp.listener->sendto(PC_PTP_GENERAL_PORT, &g_group, PC_PTP_GENERAL_PORT, buf, n);

    m_seq++;
}

// Answer a slave's Delay_Req with a Delay_Resp (t4 = when we received it).
static void master_on_delay_req(const pc_ptp_header *req)
{
    int64_t t4 = master_time_ns();
    pc_ptp_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.clock_identity, my_clock_id, 8);
    h.port_number = MY_PORT;
    h.sequence_id = req->sequence_id; // echo the requester's sequenceId
    pc_ptp_timestamp t4ts;
    pc_ptp_ts_from_ns(t4, &t4ts);
    uint8_t buf[64];
    size_t n = pc_ptp_build_delay_resp(buf, sizeof(buf), &h, &t4ts, req->clock_identity, req->port_number);
    Udp.listener->sendto(PC_PTP_GENERAL_PORT, &g_group, PC_PTP_GENERAL_PORT, buf, n);
}

static void on_event(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    pc_ptp_header h;
    if (pc_ptp_parse_header(data, len, &h) && h.message_type == PC_PTP_DELAY_REQ)
    {
        master_on_delay_req(&h);
    }
}
static void on_general(const uint8_t *, size_t, const struct pc_udp_peer *, void *)
{
}

#else // ---------------- slave ----------------

static uint16_t dreq_seq = 0;
static uint16_t sync_seq = 0;
static int64_t t1 = 0, t2 = 0, t3 = 0, t4 = 0;
static bool awaiting_t1 = false;

static void send_delay_req()
{
    pc_ptp_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.clock_identity, my_clock_id, 8);
    h.port_number = MY_PORT;
    h.sequence_id = ++dreq_seq;
    h.log_interval = 0x7F;
    pc_ptp_timestamp zero = {0, 0};
    uint8_t buf[PC_PTP_HEADER_LEN + PC_PTP_TS_LEN];
    size_t n = pc_ptp_build_delay_req(buf, sizeof(buf), &h, &zero);
    t3 = now_ns();
    Udp.listener->sendto(PC_PTP_EVENT_PORT, &g_group, PC_PTP_EVENT_PORT, buf, n);
}

static void on_event(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    pc_ptp_header h;
    if (!pc_ptp_parse_header(data, len, &h) || h.message_type != PC_PTP_SYNC)
    {
        return;
    }
    t2 = now_ns();
    sync_seq = h.sequence_id;
    if (h.flags & 0x0200) // two-step: t1 arrives in Follow_Up
    {
        awaiting_t1 = true;
    }
    else
    {
        pc_ptp_timestamp ts;
        if (pc_ptp_parse_timestamp_msg(data, len, &h, &ts))
        {
            t1 = pc_ptp_ts_to_ns(&ts);
        }
        awaiting_t1 = false;
        send_delay_req();
    }
}

static void on_general(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    pc_ptp_header h;
    if (!pc_ptp_parse_header(data, len, &h))
    {
        return;
    }
    if (h.message_type == PC_PTP_FOLLOW_UP && awaiting_t1 && h.sequence_id == sync_seq)
    {
        pc_ptp_timestamp ts;
        if (pc_ptp_parse_timestamp_msg(data, len, &h, &ts))
        {
            t1 = pc_ptp_ts_to_ns(&ts);
            awaiting_t1 = false;
            send_delay_req();
        }
    }
    else if (h.message_type == PC_PTP_DELAY_RESP)
    {
        pc_ptp_delay_resp r;
        if (pc_ptp_parse_delay_resp(data, len, &h, &r) && h.sequence_id == dreq_seq && r.req_port == MY_PORT &&
            memcmp(r.req_clock_id, my_clock_id, 8) == 0)
        {
            t4 = pc_ptp_ts_to_ns(&r.receive);
            pc_ptp_sync s;
            pc_ptp_compute(t1, t2, t3, t4, &s);
            char a[24], b[24];
            Serial.printf("PTP seq=%u  offset=%s ns  path_delay=%s ns\n", h.sequence_id, i64(s.offset_ns, a),
                          i64(s.delay_ns, b));
        }
    }
    else if (h.message_type == PC_PTP_ANNOUNCE)
    {
        pc_ptp_announce an;
        if (pc_ptp_parse_announce(data, len, &h, &an))
        {
            Serial.printf("PTP master: clockClass=%u priority1=%u stepsRemoved=%u utcOffset=%d\n", an.gm_clock_class,
                          an.gm_priority1, an.steps_removed, an.utc_offset);
        }
    }
}

#endif // PTP_MASTER

void setup()
{
    Ip.parse(PTP_GROUP, &g_group); // the tag becomes an address once, here

    Serial.begin(115200);
    delay(300);
    Serial.printf("\n=== PC PTP %s (IEEE 1588 ordinary clock) ===\n", PTP_MASTER ? "GRANDMASTER" : "slave");
    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip();
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    bool e = Udp.listener->listen_multicast(PTP_GROUP, PC_PTP_EVENT_PORT, on_event, nullptr);
    bool g = Udp.listener->listen_multicast(PTP_GROUP, PC_PTP_GENERAL_PORT, on_general, nullptr);
    Serial.printf("PTP on 319/320 (event=%d general=%d)\n", e, g);
}

void loop()
{
#if PTP_MASTER
    static uint32_t last = 0;
    if (pc_millis() - last >= 1000)
    {
        last = pc_millis();
        master_tick(); // send Announce + Sync + Follow_Up once per second
    }
#endif
    delay(10);
}
