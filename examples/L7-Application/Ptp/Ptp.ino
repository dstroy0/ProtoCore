// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Ptp.ino
 * @brief PTP / IEEE 1588 ordinary clock - grandmaster OR slave (PROTOCORE_ENABLE_PTP).
 *
 * The Precision Time Protocol keeps a LAN's clocks in lock-step. Set PTP_MASTER below:
 *
 *  - **PTP_MASTER 1 (grandmaster):** the device *sources* time and hands it out. Each second it sends
 *    Announce + Sync + Follow_Up (with its precise send time), and answers each slave's Delay_Req with
 *    a Delay_Resp. Feed `master_time_ns()` from an accurate source - a GPS fix (see the UbloxGnss
 *    example), a DS3231/PCF8523 RTC, `protocore_ntp_epoch()`, or the Time Source feature - and every slave
 *    on the LAN follows it. The Announce advertises clockClass 6 (locked to a primary reference) and
 *    timeSource GPS.
 *  - **PTP_MASTER 0 (slave):** the device follows a master, running the four-message exchange
 *    (Sync/Follow_Up/Delay_Req/Delay_Resp) and reporting offset + path delay.
 *
 * Timestamps here are taken in software (in the UDP callback / send path) with `Clock.micros()`, so the
 * accuracy is millisecond-class over Wi-Fi. Sub-microsecond PTP needs MAC hardware timestamping, which
 * this chip's Ethernet MAC supports but Wi-Fi does not - so on Ethernet this same code gets far tighter.
 *
 * Build flag (PlatformIO): `-DPROTOCORE_ENABLE_PTP=1`
 */

#define PROTOCORE_ENABLE_PTP 1
#define PTP_MASTER 0 // 1 = grandmaster (source time from GPS/RTC/...), 0 = slave (follow a master)

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "network_drivers/transport/udp/udp.h"
#include "server/clock/clock.h"
#include "network_drivers/application/ptp/ptp.h"


static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";
static const char *PTP_GROUP = "224.0.1.129"; // PTP default (primary) domain multicast address
static protocore_ip g_group;                   // PTP_GROUP, parsed once in setup()

// This clock's port identity (a locally-administered EUI-64 clockIdentity + port 1).
static uint8_t my_clock_id[8] = {0x02, 0x00, 0x00, 0xFF, 0xFE, 0x00, 0x00, 0x01};
static const uint16_t MY_PORT = 1;

// The borrows the codec and the address parser take; neither keeps state in it.
static uint8_t ptp_work[16];
static uint8_t ip_work[16];

static int64_t now_ns()
{
    Clock.micros(Clock.internal);
    return (int64_t)Clock.us * 1000;
} // local monotonic software timestamp

// Send one datagram FROM @p port (the bound PTP port) to the PTP group on the same port.
static void send_group(uint16_t port, const uint8_t *buf, size_t n)
{
    UdpListenerV.port = port;
    UdpListenerV.send_args.dst = &g_group;
    UdpListenerV.send_args.dst_port = port;
    UdpListenerV.send_args.data = buf;
    UdpListenerV.send_args.len = n;
    UdpListener.sendto(protocore_udp_listener_span());
}

static void ts_from_ns(int64_t ns, protocore_ptp_timestamp *ts)
{
    PtpV.ts_from_ns_args.ns = ns;
    PtpV.ts_from_ns_args.ts = ts;
    Ptp.ts_from_ns(ptp_work);
}

static int64_t ts_to_ns(const protocore_ptp_timestamp *ts)
{
    PtpV.ts_to_ns_args.ts = ts;
    Ptp.ts_to_ns(ptp_work);
    return PtpV.value;
}

static bool parse_header(const uint8_t *data, size_t len, protocore_ptp_header *h)
{
    PtpV.parse_header_args.s = data;
    PtpV.parse_header_args.len = len;
    PtpV.parse_header_args.h = h;
    Ptp.parse_header(ptp_work);
    return PtpV.ok;
}

static bool parse_timestamp_msg(const uint8_t *data, size_t len, protocore_ptp_header *h, protocore_ptp_timestamp *ts)
{
    PtpV.parse_timestamp_msg_args.s = data;
    PtpV.parse_timestamp_msg_args.len = len;
    PtpV.parse_timestamp_msg_args.h = h;
    PtpV.parse_timestamp_msg_args.ts = ts;
    Ptp.parse_timestamp_msg(ptp_work);
    return PtpV.ok;
}

// ------- accurate time source (grandmaster) -------
// Wire this to your reference: a GPS fix (UbloxGnss), a DS3231 RTC, protocore_ntp_epoch(), or the Time
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
    protocore_ptp_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.clock_identity, my_clock_id, 8);
    h.port_number = MY_PORT;
    h.sequence_id = m_seq;
    uint8_t buf[80];

    // Announce: advertise this grandmaster's quality + identity.
    protocore_ptp_announce a;
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
    ts_from_ns(master_time_ns(), &a.origin);
    PtpV.build_announce_args.buf = buf;
    PtpV.build_announce_args.cap = sizeof(buf);
    PtpV.build_announce_args.h = &h;
    PtpV.build_announce_args.a = &a;
    Ptp.build_announce(ptp_work);
    send_group(PROTOCORE_PTP_GENERAL_PORT, buf, PtpV.n);

    // Sync (two-step: the precise time follows in Follow_Up).
    h.flags = 0x0200; // twoStepFlag
    protocore_ptp_timestamp zero = {0, 0};
    PtpV.build_sync_args.buf = buf;
    PtpV.build_sync_args.cap = sizeof(buf);
    PtpV.build_sync_args.h = &h;
    PtpV.build_sync_args.origin = &zero;
    Ptp.build_sync(ptp_work);
    size_t n = PtpV.n;
    int64_t t1 = master_time_ns(); // precise egress instant
    send_group(PROTOCORE_PTP_EVENT_PORT, buf, n);

    // Follow_Up carries t1.
    h.flags = 0;
    protocore_ptp_timestamp ts1;
    ts_from_ns(t1, &ts1);
    PtpV.build_follow_up_args.buf = buf;
    PtpV.build_follow_up_args.cap = sizeof(buf);
    PtpV.build_follow_up_args.h = &h;
    PtpV.build_follow_up_args.precise = &ts1;
    Ptp.build_follow_up(ptp_work);
    send_group(PROTOCORE_PTP_GENERAL_PORT, buf, PtpV.n);

    m_seq++;
}

// Answer a slave's Delay_Req with a Delay_Resp (t4 = when we received it).
static void master_on_delay_req(const protocore_ptp_header *req)
{
    int64_t t4 = master_time_ns();
    protocore_ptp_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.clock_identity, my_clock_id, 8);
    h.port_number = MY_PORT;
    h.sequence_id = req->sequence_id; // echo the requester's sequenceId
    protocore_ptp_timestamp t4ts;
    ts_from_ns(t4, &t4ts);
    uint8_t buf[64];
    PtpV.build_delay_resp_args.buf = buf;
    PtpV.build_delay_resp_args.cap = sizeof(buf);
    PtpV.build_delay_resp_args.h = &h;
    PtpV.build_delay_resp_args.recv = &t4ts;
    PtpV.build_delay_resp_args.req_clock_id = req->clock_identity;
    PtpV.build_delay_resp_args.req_port = req->port_number;
    Ptp.build_delay_resp(ptp_work);
    send_group(PROTOCORE_PTP_GENERAL_PORT, buf, PtpV.n);
}

static void on_event(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    protocore_ptp_header h;
    if (parse_header(data, len, &h) && h.message_type == PROTOCORE_PTP_DELAY_REQ)
    {
        master_on_delay_req(&h);
    }
}
static void on_general(const uint8_t *, size_t, const struct protocore_udp_peer *, void *)
{
}

#else // ---------------- slave ----------------

static uint16_t dreq_seq = 0;
static uint16_t sync_seq = 0;
static int64_t t1 = 0, t2 = 0, t3 = 0, t4 = 0;
static bool awaiting_t1 = false;

static void send_delay_req()
{
    protocore_ptp_header h;
    memset(&h, 0, sizeof(h));
    memcpy(h.clock_identity, my_clock_id, 8);
    h.port_number = MY_PORT;
    h.sequence_id = ++dreq_seq;
    h.log_interval = 0x7F;
    protocore_ptp_timestamp zero = {0, 0};
    uint8_t buf[PROTOCORE_PTP_HEADER_LEN + PROTOCORE_PTP_TS_LEN];
    PtpV.build_delay_req_args.buf = buf;
    PtpV.build_delay_req_args.cap = sizeof(buf);
    PtpV.build_delay_req_args.h = &h;
    PtpV.build_delay_req_args.origin = &zero;
    Ptp.build_delay_req(ptp_work);
    size_t n = PtpV.n;
    t3 = now_ns();
    send_group(PROTOCORE_PTP_EVENT_PORT, buf, n);
}

static void on_event(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    protocore_ptp_header h;
    if (!parse_header(data, len, &h) || h.message_type != PROTOCORE_PTP_SYNC)
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
        protocore_ptp_timestamp ts;
        if (parse_timestamp_msg(data, len, &h, &ts))
        {
            t1 = ts_to_ns(&ts);
        }
        awaiting_t1 = false;
        send_delay_req();
    }
}

static void on_general(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)peer;
    (void)ctx;
    protocore_ptp_header h;
    if (!parse_header(data, len, &h))
    {
        return;
    }
    if (h.message_type == PROTOCORE_PTP_FOLLOW_UP && awaiting_t1 && h.sequence_id == sync_seq)
    {
        protocore_ptp_timestamp ts;
        if (parse_timestamp_msg(data, len, &h, &ts))
        {
            t1 = ts_to_ns(&ts);
            awaiting_t1 = false;
            send_delay_req();
        }
    }
    else if (h.message_type == PROTOCORE_PTP_DELAY_RESP)
    {
        protocore_ptp_delay_resp r;
        PtpV.parse_delay_resp_args.s = data;
        PtpV.parse_delay_resp_args.len = len;
        PtpV.parse_delay_resp_args.h = &h;
        PtpV.parse_delay_resp_args.out = &r;
        Ptp.parse_delay_resp(ptp_work);
        if (PtpV.ok && h.sequence_id == dreq_seq && r.req_port == MY_PORT && memcmp(r.req_clock_id, my_clock_id, 8) == 0)
        {
            t4 = ts_to_ns(&r.receive);
            protocore_ptp_sync s;
            PtpV.compute_args.t1 = t1;
            PtpV.compute_args.t2 = t2;
            PtpV.compute_args.t3 = t3;
            PtpV.compute_args.t4 = t4;
            PtpV.compute_args.out = &s;
            Ptp.compute(ptp_work);
            char a[24], b[24];
            Serial.printf("PTP seq=%u  offset=%s ns  path_delay=%s ns\n", h.sequence_id, i64(s.offset_ns, a),
                          i64(s.delay_ns, b));
        }
    }
    else if (h.message_type == PROTOCORE_PTP_ANNOUNCE)
    {
        protocore_ptp_announce an;
        PtpV.parse_announce_args.s = data;
        PtpV.parse_announce_args.len = len;
        PtpV.parse_announce_args.h = &h;
        PtpV.parse_announce_args.out = &an;
        Ptp.parse_announce(ptp_work);
        if (PtpV.ok)
        {
            Serial.printf("PTP master: clockClass=%u priority1=%u stepsRemoved=%u utcOffset=%d\n", an.gm_clock_class,
                          an.gm_priority1, an.steps_removed, an.utc_offset);
        }
    }
}

#endif // PTP_MASTER

void setup()
{
    IpV.args.text = PTP_GROUP;
    IpV.args.out = &g_group;
    Ip.parse(ip_work); // the tag becomes an address once, here

    Serial.begin(115200);
    delay(300);
    Serial.printf("\n=== PC PTP %s (IEEE 1588 ordinary clock) ===\n", PTP_MASTER ? "GRANDMASTER" : "slave");
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32;
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    UdpListenerV.bind.group_ip = PTP_GROUP;
    UdpListenerV.bind.handler_ctx = nullptr;
    UdpListenerV.port = PROTOCORE_PTP_EVENT_PORT;
    UdpListenerV.bind.handler = on_event;
    UdpListener.listen_multicast(protocore_udp_listener_span());
    bool e = UdpListenerV.ok;
    UdpListenerV.port = PROTOCORE_PTP_GENERAL_PORT;
    UdpListenerV.bind.handler = on_general;
    UdpListener.listen_multicast(protocore_udp_listener_span());
    bool g = UdpListenerV.ok;
    Serial.printf("PTP on 319/320 (event=%d general=%d)\n", e, g);
}

void loop()
{
#if PTP_MASTER
    static uint32_t last = 0;
    Clock.millis(Clock.internal);
    if (Clock.ms - last >= 1000)
    {
        last = Clock.ms;
        master_tick(); // send Announce + Sync + Follow_Up once per second
    }
#endif
    delay(10);
}
