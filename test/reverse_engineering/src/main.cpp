// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file main.cpp
 * @brief reverse_engineering DAQ node - pulls a high-rate signal capture from one of two
 *        front ends and streams it to dpa_cpa_network_engine.py for analysis.
 *
 * ## Two front ends, one wire protocol (daq_protocol.h)
 *
 * **DAQ_FRONTEND_SCPI_SCOPE** (the default; build env `daq_scpi_scope`). The realistic
 * case for most benches: the "high speed DAQ" is a real oscilloscope, and its output is
 * whatever `:WAVeform:DATA?` hands back over its SCPI port - with or without the scope's
 * own trigger armed (see WITH_TRIGGER below). This node is a SCPI CONTROLLER: it opens a
 * TCP connection to the scope's SCPI-RAW socket (port 5025, nearly universal across modern
 * benches - Keysight/Rigol/Siglent all speak it; a Tektronix uses very similar `:WFMOutpre`
 * mnemonics, swap the SCPI_* command macros below) via services/network_drivers/transport's
 * pc_client_*, and drives it with services/instrumentation/scpi's codec (pc_scpi_build / pc_scpi_parse_block
 * / pc_scpi_parse_number) - all of it already shipped in this library for exactly this
 * "device drives a bench instrument" role. No local capture buffer is needed: the scope
 * already did its own pre/post-trigger acquisition, so a pulled record is handed straight
 * to the network egress.
 *
 * **DAQ_FRONTEND_ADC_DMA** (build env `daq_adc_dma`). The bare-metal case for a bench with
 * no scope: an external high-speed ADC (AD9226 - 12-bit/65 MSPS single, or AD9238 - 12-bit/
 * 20-65 MSPS dual) behind an FPGA/CPLD burst-capture front end (its 12-bit parallel output
 * bus is far beyond an ESP32's reach directly - see services/peripherals/ad9238/ad9238.h) drains a
 * triggered burst into this node over SPI or UART DMA (mmgr/dma, PC_DMA_SPI /
 * PC_DMA_UART), handed off ISR-safe through the preempting work queue's DMA lane
 * (services/system/preempt_queue) to server/signaling/trace_capture, which assembles the pre/post-trigger
 * window and fires the same network egress. AD9238's SPI *configuration* port (power-down,
 * output format, test pattern - not the sample data path) is set up over the real Arduino
 * SPI library via services/peripherals/ad9238's codec, with a write/read-back self-test at boot.
 *
 * ## Analog front end (both DAQ_FRONTEND_ADC_DMA and an external scope probe)
 *
 *     NF/EM probe or shunt --> conditioning amp (fixed or VGA gain) --> ADC / scope input
 *
 * A near-field H-field probe (or a current-shunt / 1:1 isolation transformer on a power
 * rail) into a low-noise conditioning amplifier, AC-coupled so the ADC's input range holds
 * the signal's swing around 0 V rather than a DC bias eating into full scale. If your amp
 * is a digitally-controlled VGA (ADI's AD8331/AD8332 is the common pairing with an AD9226/
 * AD9238 front end in reference designs), wire its gain control into
 * @ref daq_set_frontend_gain_db below and fold the applied gain into Y_INCREMENT so the
 * network engine's volts are physical, not raw codes.
 *
 * ## Applications
 *
 * The same probe -> amp -> DAQ -> windowed-capture -> network pipeline serves side-channel
 * analysis (dpa_cpa_engine.py), non-destructive testing (ultrasonic / eddy-current / acoustic
 * emission transient capture), and general hardware reverse engineering (bus/signal capture
 * around an external trigger) equally - nothing here is SCA-specific until the Python side's
 * AES hypothesis stage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#include "daq_protocol.h"

#ifndef DAQ_FRONTEND
#define DAQ_FRONTEND DAQ_FRONTEND_SCPI_SCOPE
#endif

#include "network_drivers/application/ntp_service/ntp_service.h" // pc_ntp_* - wall-clock sync (see wall_clock_us_now())
#include "network_drivers/transport/tcp/tcp_client.h"            // pc_client_*
#include "server/clock/clock.h"                                  // pc_millis(), pcdelay(), pc_cycles_to_ns()
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#if DAQ_FRONTEND == DAQ_FRONTEND_SCPI_SCOPE
#include "services/instrumentation/scpi/scpi.h"
#else
#include "mmgr/dma.h"
#include "network_drivers/session/preempt_queue.h"
#include "server/signaling/trace_capture.h"
#include "services/peripherals/ad9238/ad9238.h"
#include <SPI.h>
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_PASSWORD"
#endif
/** @brief Where dpa_cpa_network_engine.py's SideChannelStreamReceiver listens. */
#ifndef ANALYSIS_HOST
#define ANALYSIS_HOST "192.168.1.50"
#endif
#ifndef ANALYSIS_PORT
#define ANALYSIS_PORT 8080
#endif

// ── Shared: WiFi bring-up + the network egress link (both front ends) ─────────────────────

static int g_analysis_cid = -1;
static uint32_t g_windows_dropped_total = 0;

/** @brief Weak hook: wire this to your VGA's gain-set control (e.g. an AD8331/AD8332) if the
 *         analog front end has one. Fold the applied dB into Y_INCREMENT so the network
 *         engine sees physical volts, not raw codes at an unknown gain. */
extern "C" __attribute__((weak)) void daq_set_frontend_gain_db(float db)
{
    (void)db; // no VGA wired - the amp (if any) runs at its fixed/manual gain
}

static void wifi_connect()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[*] Connecting to WiFi");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000)
    {
        delay(250);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("[+] WiFi up, IP=%s\n", WiFi.localIP().toString().c_str());
        pc_ntp_begin(); // async - pc_ntp_synced() flips true once the first reply lands
    }
    else
    {
        Serial.println("[-] WiFi connect timed out; will keep retrying the analysis link");
    }
}

// ── Wall-clock timestamping: NTP now, GNSS-upgradeable later ──────────────────────────────
//
// pc_ntp_epoch() (network_drivers/application/ntp_service) only resolves whole seconds; pc_micros() gives a
// free-running microsecond counter with no absolute meaning of its own. Combining them - an
// anchor pair (epoch, micros) re-taken every time the epoch ticks over - gives every window a
// real Unix-epoch-microsecond timestamp, not just a relative trace_id/assembly_ns: the actual
// point of it is correlating this node's captures against an INDEPENDENT clock - another DAQ
// node on a different probe/channel, or an external log the target itself writes. Accuracy is
// bounded by SNTP's usual few-ms-to-tens-of-ms over WiFi, not sample-precise.
//
// A GNSS module upgrades this same anchor scheme to sub-microsecond precision with no firmware
// changes here: this repository ships GNSS position-survey (services/timing_position/gnss) and generic NMEA
// framing (services/timing_position/nmea0183) primitives, but not yet a ready-made "parse $GPRMC's UTC field"
// time source - wire one (feed a GPS module's NMEA sentence through the existing framing codec,
// pull its UTC field, call wall_clock_anchor() with it instead of pc_ntp_epoch()) and, for the
// real precision win, route the module's PPS output into the SAME external GPIO trigger ISR
// this firmware already has (on_trigger_isr) to timestamp the anchor at a hardware edge instead
// of a polled epoch tick - multiple nodes sharing one PPS line then agree to within nanoseconds.
static uint32_t g_wall_clock_anchor_epoch = 0;
static uint32_t g_wall_clock_anchor_micros = 0;

static void wall_clock_maybe_anchor()
{
    if (!pc_ntp_synced())
    {
        return;
    }
    uint32_t epoch = (uint32_t)pc_ntp_epoch();
    if (epoch != g_wall_clock_anchor_epoch)
    {
        g_wall_clock_anchor_epoch = epoch;
        g_wall_clock_anchor_micros = pc_micros();
    }
}

/** @brief Current Unix epoch microseconds, or 0 if no time source has synced yet. */
static uint64_t wall_clock_us_now()
{
    wall_clock_maybe_anchor();
    if (g_wall_clock_anchor_epoch == 0)
    {
        return 0;
    }
    uint32_t delta_us = pc_micros() - g_wall_clock_anchor_micros; // wrap-safe unsigned delta
    return (uint64_t)g_wall_clock_anchor_epoch * 1000000ULL + delta_us;
}

static bool ensure_analysis_link()
{
    if (g_analysis_cid >= 0)
    {
        if (pc_client_connected(g_analysis_cid) && !pc_client_is_closed(g_analysis_cid))
        {
            return true;
        }
        pc_client_close(g_analysis_cid);
        g_analysis_cid = -1;
    }
    g_analysis_cid = pc_client_open(ANALYSIS_HOST, ANALYSIS_PORT, 2000);
    return g_analysis_cid >= 0;
}

/**
 * @brief Frame + send one completed window. Binary memcpy framing only, end to end - no
 *        snprintf/text formatting anywhere on this path (a text header would cost ~3x the
 *        cycles a raw struct copy does, and this runs once per window at whatever rate the
 *        front end completes them, so it matters). Best-effort: a down link drops the window
 *        (counted in the next header's windows_dropped) rather than blocking capture.
 */
static void send_window(uint8_t frontend, uint32_t trace_id, const uint8_t *samples, uint32_t n_samples,
                        uint16_t pretrigger_samples, uint8_t sample_bytes, uint8_t channel_count, float x_increment_s,
                        float sample_rate_hz, float y_increment, float y_origin, uint32_t assembly_ns)
{
    if (!ensure_analysis_link())
    {
        g_windows_dropped_total++;
        return;
    }

    DaqPacketHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, DAQ_MAGIC, 4);
    hdr.version = DAQ_PROTO_VERSION;
    hdr.msg_type = DAQ_MSG_WINDOW;
    hdr.frontend = frontend;
    hdr.trace_id = trace_id;
    hdr.n_samples = n_samples;
    hdr.pretrigger_samples = pretrigger_samples;
    hdr.sample_bytes = sample_bytes;
    hdr.channel_count = channel_count;
    hdr.x_increment_s = x_increment_s;
    hdr.sample_rate_hz = sample_rate_hz;
    hdr.y_increment = y_increment;
    hdr.y_origin = y_origin;
    hdr.windows_dropped = g_windows_dropped_total;
    hdr.assembly_ns = assembly_ns;
    hdr.wall_clock_us = wall_clock_us_now();
    hdr.payload_len = (uint16_t)((uint32_t)n_samples * sample_bytes * channel_count);
    hdr.header_crc16 = 0;
    hdr.header_crc16 = daq_crc16((const uint8_t *)&hdr, sizeof(hdr));

    uint16_t payload_crc = daq_crc16(samples, hdr.payload_len);
    uint8_t trailer[2] = {(uint8_t)(payload_crc & 0xFF), (uint8_t)(payload_crc >> 8)};

    pc_client_send(g_analysis_cid, &hdr, sizeof(hdr));
    pc_client_send(g_analysis_cid, samples, hdr.payload_len);
    pc_client_send(g_analysis_cid, trailer, sizeof(trailer));
}

#if DAQ_FRONTEND == DAQ_FRONTEND_SCPI_SCOPE
// ════════════════════════════════════════════════════════════════════════════════════════
// Front end: SCPI oscilloscope client
// ════════════════════════════════════════════════════════════════════════════════════════

#ifndef SCOPE_HOST
#define SCOPE_HOST "192.168.1.60"
#endif
#ifndef SCOPE_PORT
#define SCOPE_PORT PC_SCPI_PORT
#endif
/** @brief Arm a single triggered acquisition and wait for it before pulling :WAV:DATA? (1),
 *         or free-run - pull whatever is currently on screen every loop (0). Matches "the
 *         i/f will be the output of an oscope with or without triggering": both are the same
 *         :WAV:DATA? pull, this only decides whether we arm + wait for a trigger event first. */
#ifndef WITH_TRIGGER
#define WITH_TRIGGER 1
#endif
#ifndef SCPI_CHANNEL
#define SCPI_CHANNEL "CHAN1" // Tektronix: "CH1"
#endif
#define SCPI_WAVE_SOURCE ":WAVeform:SOURce"
#define SCPI_WAVE_FORMAT ":WAVeform:FORMat"
#define SCPI_WAVE_DATA_Q ":WAVeform:DATA?"
#define SCPI_WAVE_XINC_Q ":WAVeform:XINCrement?"
#define SCPI_WAVE_YINC_Q ":WAVeform:YINCrement?"
#define SCPI_WAVE_YORIGIN_Q ":WAVeform:YORigin?"
#define SCPI_ARM_SINGLE ":SINGle"

static int g_scope_cid = -1;
static uint32_t g_trace_id = 0;

static bool scope_link()
{
    if (g_scope_cid >= 0)
    {
        if (pc_client_connected(g_scope_cid) && !pc_client_is_closed(g_scope_cid))
        {
            return true;
        }
        pc_client_close(g_scope_cid);
        g_scope_cid = -1;
    }
    g_scope_cid = pc_client_open(SCOPE_HOST, SCOPE_PORT, 3000);
    return g_scope_cid >= 0;
}

static bool scpi_send(const char *header, const char *const *args, size_t argc)
{
    char line[128];
    size_t n = pc_scpi_build(line, sizeof(line), header, args, argc);
    if (!n)
    {
        return false;
    }
    return pc_client_send(g_scope_cid, line, n);
}

/** @brief Read up to a trailing '\n' into buf (NUL-terminated). Bounded by timeout_ms. */
static size_t scpi_read_line(char *buf, size_t cap, uint32_t timeout_ms)
{
    size_t total = 0;
    uint32_t start = pc_millis();
    while (pc_millis() - start < timeout_ms && total + 1 < cap)
    {
        if (pc_client_available(g_scope_cid) == 0)
        {
            pcdelay(2);
            continue;
        }
        size_t n = pc_client_read(g_scope_cid, (uint8_t *)buf + total, cap - 1 - total);
        total += n;
        if (n && buf[total - 1] == '\n')
        {
            break;
        }
    }
    buf[total] = '\0';
    return total;
}

/** @brief Read an IEEE 488.2 arbitrary block (#<n><len><data>) into buf; *data points into buf. */
static bool scpi_read_block(uint8_t *buf, size_t cap, const uint8_t **data, size_t *data_len, uint32_t timeout_ms)
{
    size_t total = 0;
    uint32_t start = pc_millis();
    while (pc_millis() - start < timeout_ms && total < cap)
    {
        if (pc_client_available(g_scope_cid) == 0)
        {
            pcdelay(2);
            continue;
        }
        total += pc_client_read(g_scope_cid, buf + total, cap - total);
        size_t consumed = 0;
        if (pc_scpi_parse_block(buf, total, data, data_len, &consumed))
        {
            return true;
        }
    }
    return false;
}

static bool scpi_query_number(const char *header, double *out)
{
    if (!scpi_send(header, nullptr, 0))
    {
        return false;
    }
    char line[64];
    size_t n = scpi_read_line(line, sizeof(line), 2000);
    return n && pc_scpi_parse_number(line, n, out);
}

/** @brief Block up to timeout_ms polling *OPC? for the scope to finish an armed acquisition. */
static bool scpi_wait_opc(uint32_t timeout_ms)
{
    const char *opc = pc_scpi_common(ScpiCommon::SCPI_OPC_Q);
    uint32_t start = pc_millis();
    while (pc_millis() - start < timeout_ms)
    {
        if (!scpi_send(opc, nullptr, 0))
        {
            return false;
        }
        char line[16];
        size_t n = scpi_read_line(line, sizeof(line), 500);
        bool v = false;
        if (n && pc_scpi_parse_bool(line, n, &v) && v)
        {
            return true;
        }
        pcdelay(20);
    }
    return false;
}

static uint8_t g_wave_buf[16384]; // one pulled record (payload cap; grow if your scope's :WAV:POINts is larger)

static void pull_one_record()
{
    if (!scope_link())
    {
        return;
    }

    const char *args[] = {SCPI_CHANNEL};
    scpi_send(SCPI_WAVE_SOURCE, args, 1);
    const char *fmt[] = {"BYTE"};
    scpi_send(SCPI_WAVE_FORMAT, fmt, 1);

#if WITH_TRIGGER
    scpi_send(SCPI_ARM_SINGLE, nullptr, 0);
    if (!scpi_wait_opc(5000))
    {
        return; // acquisition never completed within budget - try again next loop
    }
#endif

    double x_inc = 0, y_inc = 1, y_or = 0;
    scpi_query_number(SCPI_WAVE_XINC_Q, &x_inc);
    scpi_query_number(SCPI_WAVE_YINC_Q, &y_inc);
    scpi_query_number(SCPI_WAVE_YORIGIN_Q, &y_or);

    scpi_send(SCPI_WAVE_DATA_Q, nullptr, 0);
    const uint8_t *data = nullptr;
    size_t data_len = 0;
    if (!scpi_read_block(g_wave_buf, sizeof(g_wave_buf), &data, &data_len, 8000))
    {
        return;
    }

    // The scope already did its own pre/post-trigger acquisition; report the whole record as
    // post-trigger (pretrigger_samples=0 here is "the split already happened upstream", not
    // "no pre-roll exists" - the scope's own :TIMebase:POSition places the trigger in-record).
    send_window(DAQ_FRONTEND_SCPI_SCOPE, g_trace_id++, data, (uint32_t)data_len,
                /*pretrigger_samples=*/0, /*sample_bytes=*/1, /*channel_count=*/1, (float)x_inc,
                /*sample_rate_hz=*/0.0f, (float)y_inc, (float)y_or, /*assembly_ns=*/0);
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    wifi_connect();

    if (scope_link())
    {
        char idn[96];
        pc_scpi_build(idn, sizeof(idn), pc_scpi_common(ScpiCommon::SCPI_IDN_Q), nullptr, 0);
        pc_client_send(g_scope_cid, idn, strlen(idn));
        char reply[96];
        scpi_read_line(reply, sizeof(reply), 2000);
        Serial.printf("[+] Scope *IDN?: %s\n", reply);
    }
    else
    {
        Serial.println("[-] Could not reach the scope's SCPI-RAW port yet; will keep retrying");
    }
}

void loop()
{
    pull_one_record();
#if !WITH_TRIGGER
    pcdelay(20); // free-run: pace the poll rather than hammering the scope's SCPI parser
#endif
}

#else // DAQ_FRONTEND_ADC_DMA
// ════════════════════════════════════════════════════════════════════════════════════════
// Front end: raw high-speed ADC (any sub-GSPS part - see adc_profiles.h) via mmgr/dma +
// trace_capture. "Hold arbitrary ms of data in a ring and flush on a trigger": the pre/post
// split below is sized in MILLISECONDS and converted to samples at DAQ_SAMPLE_RATE_HZ.
//
// DAQ_SAMPLE_RATE_HZ is deliberately NOT DAQ_ADC_MAX_SAMPLE_RATE_HZ. The ADC's silicon max
// (tens of MSPS) is how fast the FPGA/CPLD front end's OWN block RAM samples; holding even
// one millisecond of that on the ESP32 itself (tens of thousands of samples) usually will
// not fit internal SRAM, and there is no reason it should - the FPGA/CPLD already IS a
// pre/post-trigger ring at full rate. DAQ_SAMPLE_RATE_HZ here is what THIS node's own ring
// runs at once fed: either a decimated/subsampled tap off the ADC (a common front-end
// feature) or a genuinely lower-rate serial DAQ source. If your front end instead captures
// the whole burst upstream and just drains it to the ESP32, skip trace_capture's ring role
// entirely - call pc_tc_trigger() once, then pc_tc_feed() the drained burst as it arrives
// (posttrigger_samples = the burst length, pretrigger_samples = 0); the API supports both.

#include "adc_profiles.h"

#define TRIGGER_PIN 4
#define AD9238_CS_PIN 5
#ifndef DAQ_PRETRIGGER_MS
#if DAQ_ADC_INTERNAL_POLLED
#define DAQ_PRETRIGGER_MS 20.0f // "capture what happened right before the sound" - a real lead-in
#else
#define DAQ_PRETRIGGER_MS 2.0f
#endif
#endif
#ifndef DAQ_POSTTRIGGER_MS
#if DAQ_ADC_INTERNAL_POLLED
#define DAQ_POSTTRIGGER_MS 200.0f // long enough to hold a whole click/clunk/coil-whine burst
#else
#define DAQ_POSTTRIGGER_MS 6.0f
#endif
#endif
#ifndef DAQ_SAMPLE_RATE_HZ
#if DAQ_ADC_INTERNAL_POLLED
#define DAQ_SAMPLE_RATE_HZ 20000.0f // a paced analogRead() polling rate - solidly audio-band
#else
#define DAQ_SAMPLE_RATE_HZ 2000000.0f // this node's own ring rate (decimated tap or a slower serial DAQ) - see above
#endif
#endif
#ifndef DAQ_VREF_VOLTS
#define DAQ_VREF_VOLTS 2.0f // internal reference full-scale (datasheet-typical; confirm your part)
#endif
#ifndef FRONTEND_GAIN_LINEAR
#define FRONTEND_GAIN_LINEAR 1.0f // fold your conditioning amp's fixed gain in here if it has no VGA control
#endif
#define DAQ_PRETRIGGER_SAMPLES ((uint32_t)(DAQ_PRETRIGGER_MS * 0.001f * DAQ_SAMPLE_RATE_HZ))
#define DAQ_POSTTRIGGER_SAMPLES ((uint32_t)(DAQ_POSTTRIGGER_MS * 0.001f * DAQ_SAMPLE_RATE_HZ))
static const float Y_INCREMENT =
    (DAQ_VREF_VOLTS / (float)DAQ_ADC_FULL_SCALE_CODES) / FRONTEND_GAIN_LINEAR; // volts/code

static_assert((uint32_t)DAQ_PRETRIGGER_SAMPLES + (uint32_t)DAQ_POSTTRIGGER_SAMPLES <= PC_TC_MAX_WINDOW_SAMPLES,
              "DAQ_PRETRIGGER_MS + DAQ_POSTTRIGGER_MS at DAQ_SAMPLE_RATE_HZ must fit PC_TC_MAX_WINDOW_SAMPLES "
              "(raise PC_TC_MAX_WINDOW_SAMPLES, or shrink the ms window / sample rate)");

static void on_window(const pc_tc_window *w, void *)
{
    uint32_t cpu_mhz = getCpuFrequencyMhz();
    uint32_t ns = pc_cycles_to_ns(w->assembly_cycles, cpu_mhz);
    send_window(DAQ_FRONTEND_ADC_DMA, w->trace_id, (const uint8_t *)w->samples, w->n_samples, w->pretrigger_samples,
                /*sample_bytes=*/2, /*channel_count=*/1, /*x_increment_s=*/0.0f, DAQ_SAMPLE_RATE_HZ, Y_INCREMENT,
                /*y_origin=*/0.0f, ns);
}

#if !DAQ_ADC_INTERNAL_POLLED
// ── DMA-drained ingest (AD9226/AD9238-class front end over SPI/UART DMA) ──────────────────

// dma_msg must fit PC_PQ_ITEM_SIZE (platformio.ini keeps the two in sync): the DMA-complete
// ISR copies the just-filled ping-pong buffer's bytes here (see dma.h - a deferred consumer
// cannot keep the event's pointer, so this copy has to happen in the ISR callback itself).
struct dma_msg
{
    uint16_t len;
    uint8_t bytes[PC_DMA_BUF_SIZE];
};
union pq_item {
    dma_msg msg;
    uint8_t raw[PC_PQ_ITEM_SIZE];
};
static_assert(sizeof(pq_item) <= PC_PQ_ITEM_SIZE, "dma_msg must fit PC_PQ_ITEM_SIZE");

// Runs in the DMA lane's high-priority task (not the ISR) - safe to do the samples_be16 ->
// uint16 unpack + feed() here. pc_tc_feed() is O(n) bounded and itself ISR-safe, so it would
// also be legal to call straight from on_dma_complete(); routing it through the preempting
// queue keeps the ISR itself down to the tiny memcpy the library's own docs call for.
static void on_dma_frame(const void *item, void *)
{
    const dma_msg *m = &((const pq_item *)item)->msg;
    uint16_t samples[PC_DMA_BUF_SIZE / 2];
    uint16_t n = m->len / 2;
    for (uint16_t i = 0; i < n; i++)
    {
        samples[i] = (uint16_t)(m->bytes[2 * i] | (m->bytes[2 * i + 1] << 8)); // little-endian 12-bit-in-16 codes
    }
    pc_tc_feed(samples, n);
}

static void on_dma_complete(const pc_dma_event *ev, void *)
{
    if (ev->dir != pc_dma_dir::PC_DMA_RX)
    {
        return;
    }
    pq_item item;
    memset(&item, 0, sizeof(item));
    uint16_t n = (ev->len < sizeof(item.msg.bytes)) ? ev->len : sizeof(item.msg.bytes);
    item.msg.len = n;
    memcpy(item.msg.bytes, ev->data, n); // must copy now - ev->data is only valid until the next completion
    pc_pq_post_lane_from_isr(pc_pq_lane::PC_PQ_LANE_DMA, &item);
}

void IRAM_ATTR on_trigger_isr()
{
    pc_tc_trigger();
}

#else // DAQ_ADC_INTERNAL_POLLED
// ── Internal-ADC polled ingest (no external chip - a $1-2 piezo/electret mic on one pin) ──
//
// No DMA peripheral to drive - analogRead() is a blocking, single-shot conversion, so a
// dedicated task just paces itself to DAQ_SAMPLE_RATE_HZ and calls pc_tc_feed() one sample
// at a time. Alongside (or instead of) the external GPIO trigger, a rolling-RMS amplitude
// trigger arms the capture the instant the input gets louder than the ambient noise floor -
// "capture what happens right when you make a sound" needs no external hardware trigger line
// at all, which is exactly the point of this front end.

#ifndef AUDIO_ADC_PIN
#define AUDIO_ADC_PIN 34 // an ADC1 input pin (ADC1 stays available with WiFi active; ADC2 does not)
#endif
#ifndef AUDIO_AUTO_TRIGGER
#define AUDIO_AUTO_TRIGGER 1 // 1 = rolling-RMS amplitude trigger; 0 = external GPIO trigger only
#endif
#ifndef AUDIO_RMS_WINDOW
#define AUDIO_RMS_WINDOW 32 // samples averaged for the rolling RMS envelope
#endif
#ifndef AUDIO_TRIGGER_MARGIN_CODES
#define AUDIO_TRIGGER_MARGIN_CODES 80.0f // RMS must rise this many codes above the learned floor to arm
#endif

void IRAM_ATTR on_trigger_isr()
{
    pc_tc_trigger();
}

static void audio_poll_task(void *)
{
    pinMode(AUDIO_ADC_PIN, INPUT);
    const uint32_t period_us = (uint32_t)(1000000.0f / DAQ_SAMPLE_RATE_HZ);

    float rms_floor = 0.0f; // a slow-moving estimate of the ambient noise floor
    float sq_accum = 0.0f;  // sum of squared deviations over the current rolling window
    uint16_t window_n = 0;
    bool floor_seeded = false;

    uint32_t next_us = micros();
    for (;;)
    {
        uint16_t code = (uint16_t)analogRead(AUDIO_ADC_PIN); // 0..4095 on the ESP32's 12-bit ADC
        pc_tc_feed(&code, 1);

#if AUDIO_AUTO_TRIGGER
        float centered = (float)code - (DAQ_ADC_FULL_SCALE_CODES / 2.0f);
        sq_accum += centered * centered;
        window_n++;
        if (window_n >= AUDIO_RMS_WINDOW)
        {
            float rms = sqrtf(sq_accum / window_n);
            sq_accum = 0.0f;
            window_n = 0;
            if (!floor_seeded)
            {
                rms_floor = rms;
                floor_seeded = true;
            }
            else if (rms > rms_floor + AUDIO_TRIGGER_MARGIN_CODES && !pc_tc_capturing())
            {
                pc_tc_trigger();
            }
            else if (!pc_tc_capturing())
            {
                rms_floor = 0.98f * rms_floor + 0.02f * rms; // track slow ambient drift, not the event itself
            }
        }
#endif

        next_us += period_us;
        int32_t remaining = (int32_t)(next_us - micros());
        if (remaining > 0)
        {
            delayMicroseconds((uint32_t)remaining); // pacing only - this is not a DMA-grade deterministic clock
        }
    }
}
#endif // DAQ_ADC_INTERNAL_POLLED

#if DAQ_ADC_HAS_SPI_CONFIG
/** @brief Configure the AD9238's SPI control port: power up, offset binary, no test pattern -
 *         then read back the power-down register to confirm the SPI link actually works. This
 *         is real SPI I/O (the Arduino SPI library), unlike the bulk DMA sample path below,
 *         which stays on the shipped ingress/egress simulator until a real pc_dma_hw_*
 *         backend is written and hardware-verified (docs/KNOWN_LIMITATIONS.md). */
static bool ad9238_bringup()
{
    SPI.begin();
    pinMode(AD9238_CS_PIN, OUTPUT);
    digitalWrite(AD9238_CS_PIN, HIGH);
    SPISettings settings(1000000, MSBFIRST, SPI_MODE0); // config port only - well under any SCLK max

    auto xfer = [&](const uint8_t *out, size_t n, uint8_t *in) {
        SPI.beginTransaction(settings);
        digitalWrite(AD9238_CS_PIN, LOW);
        for (size_t i = 0; i < n; i++)
        {
            uint8_t rx = SPI.transfer(out[i]);
            if (in)
            {
                in[i] = rx;
            }
        }
        digitalWrite(AD9238_CS_PIN, HIGH);
        SPI.endTransaction();
    };

    uint8_t buf[3];
    pc_ad9238_build_write((uint16_t)Ad9238Reg::AD9238_REG_POWER_DOWN, 0x00, buf, sizeof(buf)); // 0 = powered up
    xfer(buf, sizeof(buf), nullptr);
    pc_ad9238_build_write((uint16_t)Ad9238Reg::AD9238_REG_OUTPUT_MODE,
                          (uint8_t)Ad9238OutputFormat::AD9238_FORMAT_OFFSET_BINARY, buf, sizeof(buf));
    xfer(buf, sizeof(buf), nullptr);
    pc_ad9238_build_write((uint16_t)Ad9238Reg::AD9238_REG_TEST_IO, (uint8_t)Ad9238TestPattern::AD9238_TEST_OFF, buf,
                          sizeof(buf));
    xfer(buf, sizeof(buf), nullptr);
    uint8_t xfer_buf[3];
    pc_ad9238_build_transfer(xfer_buf, sizeof(xfer_buf)); // latch the shadowed writes above
    xfer(xfer_buf, sizeof(xfer_buf), nullptr);

    uint8_t rd[3] = {0, 0, 0};
    uint8_t rd_hdr[2];
    pc_ad9238_build_read((uint16_t)Ad9238Reg::AD9238_REG_POWER_DOWN, rd_hdr, sizeof(rd_hdr));
    uint8_t tx3[3] = {rd_hdr[0], rd_hdr[1], 0x00};
    xfer(tx3, 3, rd);
    return rd[2] == 0x00; // read back what we wrote (powered up)
}
#else
/** @brief The selected DAQ_ADC_PROFILE has no SPI port (e.g. AD9226 - pure parallel output,
 *         "does not need to be configured by software" per its datasheet): nothing to bring up. */
static bool ad9238_bringup()
{
    return true;
}
#endif // DAQ_ADC_HAS_SPI_CONFIG

void setup()
{
    Serial.begin(115200);
    delay(300);
    wifi_connect();

#if DAQ_ADC_HAS_SPI_CONFIG
    bool ad9238_ok = ad9238_bringup();
    Serial.printf("[%c] AD9238 SPI config-port bring-up (write/read-back)\n", ad9238_ok ? '+' : '-');
#else
    Serial.println("[*] Selected ADC profile has no SPI config port - nothing to bring up");
#endif
    // Pipeline latency is in the ADC's OWN encode-clock domain (DAQ_ADC_MAX_SAMPLE_RATE_HZ),
    // not this node's ring rate (DAQ_SAMPLE_RATE_HZ) - see the two clock domains note above.
    Serial.printf("[*] ADC pipeline latency: %d encode cycles (%.1f ns @ %.1f MSPS) - the burst-capture\n",
                  DAQ_ADC_PIPELINE_LATENCY_SAMPLES,
                  (double)DAQ_ADC_PIPELINE_LATENCY_SAMPLES * 1e9 / DAQ_ADC_MAX_SAMPLE_RATE_HZ,
                  DAQ_ADC_MAX_SAMPLE_RATE_HZ / 1e6);
    Serial.println("    front end should shift its trigger index back by this much before draining.");

    pc_tc_config tc = {};
    tc.pretrigger_samples = DAQ_PRETRIGGER_SAMPLES;
    tc.posttrigger_samples = DAQ_POSTTRIGGER_SAMPLES;
    tc.sink = on_window;
    if (!pc_tc_begin(&tc))
    {
        Serial.println("[-] trace_capture begin() failed - check PC_TC_MAX_WINDOW_SAMPLES sizing");
        return;
    }

#if !DAQ_ADC_INTERNAL_POLLED
    pc_pq_config pq = {};
    pq.handler = on_dma_frame;
    pq.priority = 0; // lane default (DMA lane ranks above the user lane)
    pq.core = 1;
    pq.name = "daq_rx";
    if (!pc_pq_start_lane(pc_pq_lane::PC_PQ_LANE_DMA, &pq))
    {
        Serial.println("[-] preempt queue DMA lane failed to start");
        return;
    }

    pc_dma_config ch = {};
    ch.channel = 0;
    ch.periph = pc_dma_periph::PC_DMA_SPI; // swap to PC_DMA_UART for a serial-streaming DAQ source
    ch.loopback = false;
    ch.on_complete = on_dma_complete;
    if (!pc_dma_open(&ch))
    {
        Serial.println("[-] dma channel failed to open");
        return;
    }

    pinMode(TRIGGER_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), on_trigger_isr, RISING);

    Serial.println("[*] armed: DMA ingest -> preempt queue -> trace_capture -> network egress");
#if PC_DMA_SIMULATE
    Serial.println("[*] PC_DMA_SIMULATE=1 (the shipped, tested backend) - wire pc_dma_hw_* for real");
    Serial.println("    silicon once bench-verified; see docs/KNOWN_LIMITATIONS.md.");
#endif
#else // DAQ_ADC_INTERNAL_POLLED
    pinMode(TRIGGER_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), on_trigger_isr, RISING);

    xTaskCreatePinnedToCore(audio_poll_task, "audio_poll", 4096, nullptr, 3, nullptr, 1);
    Serial.printf("[*] armed: analogRead(pin %d) @ %.0f Hz -> trace_capture -> network egress\n", AUDIO_ADC_PIN,
                  DAQ_SAMPLE_RATE_HZ);
#if AUDIO_AUTO_TRIGGER
    Serial.println("    auto-trigger: arms on a rolling-RMS rise over the learned ambient floor");
    Serial.println("    (plus the external GPIO trigger below, if you also wire one).");
#endif
#endif // DAQ_ADC_INTERNAL_POLLED
}

void loop()
{
#if !DAQ_ADC_INTERNAL_POLLED
    pc_dma_poll(); // no-op on real silicon (ISRs drive it there); steps the simulator otherwise
#endif

    static uint32_t last_stats = 0;
    if (pc_millis() - last_stats > 5000)
    {
        last_stats = pc_millis();
        pc_tc_stats st;
        pc_tc_get_stats(&st);
        g_windows_dropped_total = st.triggers_dropped + st.samples_dropped;
        Serial.printf("[stats] windows=%u triggers_dropped=%u samples_dropped=%u\n", (unsigned)st.windows_completed,
                      (unsigned)st.triggers_dropped, (unsigned)st.samples_dropped);
    }
}

#endif // DAQ_FRONTEND
