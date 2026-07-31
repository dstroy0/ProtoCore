// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the MQTT 3.1.1 packet codec (services/iot/mqtt): the pure
// mqtt_build_* / mqtt_parse_* / *_remlen functions that serialize and deserialize the wire packets
// (CONNECT, PUBLISH, SUBSCRIBE, the fixed header, the Remaining Length varint). These are the
// host-testable half of the service - no sockets, no heap, no TLS - so every call here exercises the
// exact production code path, just like the modbus worked example. Deliberately out of scope: the
// ESP32-only transport half (pc_mqtt_connect / publish / subscribe / loop), which drives a real
// lwIP/mbedTLS broker session; this rig has no network peer, so the QoS state machine and socket I/O
// are never invoked - only the deterministic CPU-side codec is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/mqtt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/mqtt/mqtt.h"
#include <Arduino.h>
#include <string.h>

// A realistic ~55-byte JSON telemetry payload (the kind of message a sensor node PUBLISHes).
static const uint8_t kPubPayload[] = "{\"t\":23.5,\"h\":48.2,\"p\":1013.2,\"batt\":97,\"ts\":1721764800}";
#define PUB_PAYLOAD_LEN (sizeof(kPubPayload) - 1) // drop the string NUL

static void mqtt_bench_task(void *)
{
    // A full CONNECT (credentials + Last-Will), mirroring test_connect_full in test/test_mqtt.
    MqttConnectOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.client_id = "esp32-sensor-01";
    opts.user = "device";
    opts.pass = "s3cr3t";
    opts.keepalive_s = 30;
    opts.clean_session = true;
    opts.will_topic = "devices/esp32-sensor-01/status";
    opts.will_msg = (const uint8_t *)"offline";
    opts.will_len = 7;
    opts.will_qos = 1;
    opts.will_retain = true;

    static const char *kPubTopic = "devices/esp32-sensor-01/telemetry";
    static const char *kSubFilter = "devices/+/telemetry";

    static uint8_t out[512]; // encode scratch

    // Pre-build one spec-conformant QoS-1 PUBLISH so the parse benches have a stable, known-good
    // input, then split off its fixed header once (the bytes the parsers actually consume).
    static uint8_t pkt[512];
    size_t pkt_len =
        pc_mqtt_build_publish(pkt, sizeof(pkt), kPubTopic, kPubPayload, PUB_PAYLOAD_LEN, 1, 0x1234, false, false);
    uint8_t p_type = 0, p_flags = 0;
    uint32_t p_rl = 0;
    size_t p_hl = 0;
    pc_mqtt_parse_fixed_header(pkt, pkt_len, &p_type, &p_flags, &p_rl, &p_hl);

    char topic[64];
    size_t tlen = 0, plen = 0;
    const uint8_t *payload = nullptr;
    uint16_t pid = 0;
    uint8_t rlbuf[4];

    for (;;)
    {
        Serial.printf("DB ==== mqtt device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        // Encode side: build the four packet shapes a live client emits most.
        DBENCH_OP("pc_mqtt_build_connect", 50000, sink += pc_mqtt_build_connect(out, sizeof(out), &opts));
        DBENCH_BULK("pc_mqtt_build_publish q1", 50000, PUB_PAYLOAD_LEN,
                    sink += pc_mqtt_build_publish(out, sizeof(out), kPubTopic, kPubPayload, PUB_PAYLOAD_LEN, 1, 0x1234,
                                                  false, false));
        DBENCH_OP("pc_mqtt_build_subscribe", 100000,
                  sink += pc_mqtt_build_subscribe(out, sizeof(out), 10, kSubFilter, 1));
        DBENCH_OP("pc_mqtt_encode_remlen", 200000, sink += pc_mqtt_encode_remlen(rlbuf, 268435455u));
        // Decode side: parse the fixed header, then the PUBLISH variable header + payload.
        DBENCH_OP("pc_mqtt_parse_fixed_header", 200000,
                  sink += pc_mqtt_parse_fixed_header(pkt, pkt_len, &p_type, &p_flags, &p_rl, &p_hl));
        DBENCH_OP("pc_mqtt_parse_publish", 100000,
                  sink +=
                  pc_mqtt_parse_publish(pkt + p_hl, p_rl, p_flags, topic, sizeof(topic), &tlen, &payload, &plen, &pid));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: mqtt device microbench");
    xTaskCreatePinnedToCore(mqtt_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
