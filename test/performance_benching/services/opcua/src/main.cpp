// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OPC UA Binary server codec (services/fieldbus/opcua): the pure,
// heap-free, stdlib-free UACP framing + built-in-type codec + service builders that OPC UA Part 6
// defines. Benched here are the six hottest pure paths a ConnProto::PROTO_OPCUA connection drives:
//   - pc_opcua_parse_hello   : parse a client `HEL` (UACP header + the five negotiated sizes),
//   - pc_opcua_build_ack     : negotiate buffer sizes down to PC_OPCUA_BUF and emit the `ACK`,
//   - pc_opcua_parse_open    : parse an `OPN` OpenSecureChannelRequest (SecurityPolicy None),
//   - pc_opcua_build_open_response : build the OpenSecureChannelResponse (channel id + token),
//   - pc_opcua_parse_read    : parse a `MSG` ReadRequest (envelope + NodesToRead),
//   - pc_opcua_build_read_response : encode one Variant/DataValue per read node.
// Every call exercises the real production code path (contrast with performance_benching/device/ads1115, a
// peripheral driver where the bus transaction is stubbed). The input messages are assembled once at
// task start with the same UaWriter primitives test/test_opcua/test_opcua.cpp's build_hello /
// build_open / build_read helpers use, so the benched parses/builds run on spec-conformant,
// known-good bytes.
//
// Out of scope (deliberately not benched): the ESP32 TCP data handler pc_opcua_rx() and its
// transport ring plumbing (pc_conn_*) - that is real socket I/O this rig has no peer for; only the
// deterministic CPU-side codec/framing/builders are timed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/opcua -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/opcua/opcua.h"
#include <Arduino.h>

// Build a `HEL` Hello (mirrors test_opcua.cpp build_hello): UACP header + 5 x UInt32 + EndpointUrl.
static size_t build_hello(uint8_t *out, size_t cap, uint32_t recv, uint32_t send, uint32_t maxmsg)
{
    UaWriter w = {out, cap, 0, true};
    pc_ua_w_u8(&w, 'H');
    pc_ua_w_u8(&w, 'E');
    pc_ua_w_u8(&w, 'L');
    pc_ua_w_u8(&w, 'F');
    pc_ua_w_u32(&w, 0); // size placeholder
    pc_ua_w_u32(&w, 0); // ProtocolVersion
    pc_ua_w_u32(&w, recv);
    pc_ua_w_u32(&w, send);
    pc_ua_w_u32(&w, maxmsg);
    pc_ua_w_u32(&w, 0); // MaxChunkCount
    pc_ua_w_string(&w, "opc.tcp://host:4840", 19);
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = out[7] = 0;
    return w.n;
}

// Build a minimal `OPN` OpenSecureChannelRequest (mirrors test_opcua.cpp build_open, SecurityPolicy None).
static size_t build_open(uint8_t *out, size_t cap, uint32_t channel, uint32_t seq, uint32_t req_id, uint32_t handle,
                         uint32_t mode, uint32_t lifetime)
{
    UaWriter w = {out, cap, 0, true};
    pc_ua_w_u8(&w, 'O');
    pc_ua_w_u8(&w, 'P');
    pc_ua_w_u8(&w, 'N');
    pc_ua_w_u8(&w, 'F');
    pc_ua_w_u32(&w, 0); // size placeholder
    // Asymmetric security header.
    pc_ua_w_u32(&w, channel);
    const char *pol = OPCUA_POLICY_NONE_URI;
    pc_ua_w_string(&w, pol, (int32_t)strlen(pol));
    pc_ua_w_string(&w, nullptr, -1); // SenderCertificate
    pc_ua_w_string(&w, nullptr, -1); // ReceiverCertificateThumbprint
    // Sequence header.
    pc_ua_w_u32(&w, seq);
    pc_ua_w_u32(&w, req_id);
    // Body TypeId.
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_OPEN_REQ);
    // RequestHeader.
    pc_ua_w_nodeid_numeric(&w, 0, 0); // AuthenticationToken (null)
    pc_ua_w_u64(&w, 0);               // Timestamp
    pc_ua_w_u32(&w, handle);          // RequestHandle
    pc_ua_w_u32(&w, 0);               // ReturnDiagnostics
    pc_ua_w_string(&w, nullptr, -1);  // AuditEntryId
    pc_ua_w_u32(&w, 0);               // TimeoutHint
    pc_ua_w_nodeid_numeric(&w, 0, 0); // AdditionalHeader: null NodeId ...
    pc_ua_w_u8(&w, 0x00);             // ... + no body
    // OpenSecureChannelRequest body.
    pc_ua_w_u32(&w, 0);              // ClientProtocolVersion
    pc_ua_w_u32(&w, 0);              // RequestType = Issue
    pc_ua_w_u32(&w, mode);           // MessageSecurityMode
    pc_ua_w_string(&w, nullptr, -1); // ClientNonce
    pc_ua_w_u32(&w, lifetime);
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = out[7] = 0;
    return w.n;
}

// Build a `MSG` ReadRequest reading ns=1 numeric ids at AttributeId=Value (mirrors test_opcua.cpp build_read).
static size_t build_read(uint8_t *out, size_t cap, uint32_t token, uint32_t seq, uint32_t req_id, uint32_t handle,
                         const uint32_t *ids, uint32_t n)
{
    UaWriter w = {out, cap, 0, true};
    pc_ua_w_u8(&w, 'M');
    pc_ua_w_u8(&w, 'S');
    pc_ua_w_u8(&w, 'G');
    pc_ua_w_u8(&w, 'F');
    pc_ua_w_u32(&w, 0);
    pc_ua_w_u32(&w, 0); // SecureChannelId
    pc_ua_w_u32(&w, token);
    pc_ua_w_u32(&w, seq);
    pc_ua_w_u32(&w, req_id);
    pc_ua_w_nodeid_numeric(&w, 0, OPCUA_ID_READ_REQ);
    // RequestHeader
    pc_ua_w_nodeid_numeric(&w, 0, 0);
    pc_ua_w_u64(&w, 0);
    pc_ua_w_u32(&w, handle);
    pc_ua_w_u32(&w, 0);
    pc_ua_w_string(&w, nullptr, -1);
    pc_ua_w_u32(&w, 0);
    pc_ua_w_nodeid_numeric(&w, 0, 0);
    pc_ua_w_u8(&w, 0x00);
    // ReadRequest body
    pc_ua_w_f64(&w, 0.0);        // MaxAge
    pc_ua_w_u32(&w, 0);          // TimestampsToReturn
    pc_ua_w_i32(&w, (int32_t)n); // NodesToRead count
    for (uint32_t i = 0; i < n; i++)
    {
        pc_ua_w_nodeid_numeric(&w, 1, ids[i]); // NodeId (ns=1)
        pc_ua_w_u32(&w, OPCUA_ATTR_VALUE);     // AttributeId
        pc_ua_w_string(&w, nullptr, -1);       // IndexRange
        pc_ua_w_u16(&w, 0);                    // DataEncoding QualifiedName.ns
        pc_ua_w_string(&w, nullptr, -1);       // QualifiedName.name
    }
    out[4] = (uint8_t)w.n;
    out[5] = (uint8_t)(w.n >> 8);
    out[6] = out[7] = 0;
    return w.n;
}

static void opcua_bench_task(void *)
{
    // ---- Inputs, assembled once from the known-good test encoders. ----
    static uint8_t hel[128];
    const size_t hel_n = build_hello(hel, sizeof(hel), 65535, 65535, 0);
    OpcUaHello hello;
    pc_opcua_parse_hello(hel, hel_n, &hello); // seed a Hello for the ACK builder

    static uint8_t opn[256];
    const size_t opn_n = build_open(opn, sizeof(opn), 0, 1, 7, 42, 1, 600000);
    OpcUaOpenChannel oc;
    pc_opcua_parse_open(opn, opn_n, &oc); // seed an OpenChannel request for the OPN response builder

    static const uint32_t read_ids[2] = {1001, 1002};
    static uint8_t rdreq[256];
    const size_t rd_n = build_read(rdreq, sizeof(rdreq), 7, 5, 200, 60, read_ids, 2);
    static OpcUaReadRequest rr;
    pc_opcua_parse_read(rdreq, rd_n, &rr); // seed a parsed ReadRequest for the ReadResponse builder

    // Two DataValues: a Good Int32 and a BadNodeIdUnknown null (mirrors test_build_read_response).
    static OpcUaVariant vals[2];
    static uint32_t sts[2];
    memset(vals, 0, sizeof(vals));
    vals[0].type = OpcUaVariantType::OPCUA_VAR_INT32;
    vals[0].i32 = 4242;
    sts[0] = OPCUA_STATUS_GOOD;
    vals[1].type = OpcUaVariantType::OPCUA_VAR_NULL;
    sts[1] = OPCUA_STATUS_BAD_NODE_ID_UNKNOWN;

    // ---- Output scratch. ----
    static uint8_t ack[64];
    static uint8_t opnresp[256];
    static uint8_t rdresp[256];
    const int64_t now = pc_opcua_filetime_from_unix(1700000000LL);

    for (;;)
    {
        Serial.printf("DB ==== opcua device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        DBENCH_OP("pc_opcua_parse_hello", 50000, sink += pc_opcua_parse_hello(hel, hel_n, &hello));
        DBENCH_OP("pc_opcua_build_ack", 50000, sink += pc_opcua_build_ack(&hello, ack, sizeof(ack)));
        DBENCH_OP("pc_opcua_parse_open", 30000, sink += pc_opcua_parse_open(opn, opn_n, &oc));
        DBENCH_OP("pc_opcua_build_open_response", 30000,
                  sink += pc_opcua_build_open_response(&oc, 55, 99, 1, now, 600000, opnresp, sizeof(opnresp)));
        DBENCH_OP("pc_opcua_parse_read", 30000, sink += pc_opcua_parse_read(rdreq, rd_n, &rr));
        DBENCH_OP("pc_opcua_build_read_response", 30000,
                  sink += pc_opcua_build_read_response(&rr, vals, sts, 9, now, rdresp, sizeof(rdresp)));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: opcua device microbench");
    xTaskCreatePinnedToCore(opcua_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
