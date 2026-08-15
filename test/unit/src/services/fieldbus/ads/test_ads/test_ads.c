// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Beckhoff ADS / AMS codec (services/fieldbus/ads/ads.h).
//
// The load-bearing case is test_ams_header_octet_layout. The Beckhoff AMS specification fixes the
// 38-octet preamble octet by octet - AMS/TCP reserved(2) + length(4), then target-before-source
// AMSNetId + port, command id, state flags, cbData, error, invoke id - and every field is
// little-endian. That case writes the whole expected frame out as literal octets derived from the
// published layout, so a swapped target/source pair, a big-endian port, or a length that forgets to
// exclude the AMS/TCP header cannot pass. The Beckhoff spec text itself is not redistributable, so
// the numeric anchors quoted below are the published constants: TCP port 48898, command ids 1..9,
// state flags 0x0004 request / 0x0005 response, and the well-known symbol index groups 0xF003/0xF005.

#include "services/fieldbus/ads/ads.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// One route: AMSNetId 5.18.3.4.1.1 port 851 (the first TwinCAT 3 PLC runtime) answering a client at
// 192.168.1.10.1.1 port 33000.
static AdsRequest route(void)
{
    AdsRequest r;
    static const uint8_t TARGET[ADS_NET_ID_LEN] = {5, 18, 3, 4, 1, 1};
    static const uint8_t SOURCE[ADS_NET_ID_LEN] = {192, 168, 1, 10, 1, 1};
    memcpy(r.target.net_id, TARGET, ADS_NET_ID_LEN);
    r.target.port = 851;
    memcpy(r.source.net_id, SOURCE, ADS_NET_ID_LEN);
    r.source.port = 33000;
    r.invoke_id = 0x12345678u;
    return r;
}

// The AMS/TCP port is 0xBF02 = 48898, and the header widths add up to the 38-octet preamble.
void test_published_constants(void)
{
    TEST_ASSERT_EQUAL_UINT16(48898u, ADS_TCP_PORT);
    TEST_ASSERT_EQUAL_HEX16(0xBF02u, ADS_TCP_PORT);
    TEST_ASSERT_EQUAL_INT(6, ADS_AMSTCP_HDR_LEN);
    TEST_ASSERT_EQUAL_INT(32, ADS_AMS_HDR_LEN);
    TEST_ASSERT_EQUAL_INT(38, ADS_HDR_LEN);
    TEST_ASSERT_EQUAL_INT(ADS_AMSTCP_HDR_LEN + ADS_AMS_HDR_LEN, ADS_HDR_LEN);

    // Command ids are the AMS numbering, 1..9 with no gaps.
    TEST_ASSERT_EQUAL_INT(1, ADS_COMMAND_READ_DEVICE_INFO);
    TEST_ASSERT_EQUAL_INT(2, ADS_COMMAND_READ);
    TEST_ASSERT_EQUAL_INT(3, ADS_COMMAND_WRITE);
    TEST_ASSERT_EQUAL_INT(4, ADS_COMMAND_READ_STATE);
    TEST_ASSERT_EQUAL_INT(5, ADS_COMMAND_WRITE_CONTROL);
    TEST_ASSERT_EQUAL_INT(6, ADS_COMMAND_ADD_NOTIFICATION);
    TEST_ASSERT_EQUAL_INT(7, ADS_COMMAND_DEL_NOTIFICATION);
    TEST_ASSERT_EQUAL_INT(8, ADS_COMMAND_NOTIFICATION);
    TEST_ASSERT_EQUAL_INT(9, ADS_COMMAND_READ_WRITE);

    // A request carries the ADS-command bit alone; a reply ORs in the response bit.
    TEST_ASSERT_EQUAL_HEX16(0x0004u, ADS_STATE_REQUEST);
    TEST_ASSERT_EQUAL_HEX16(0x0005u, ADS_STATE_REPLY);
    TEST_ASSERT_EQUAL_HEX16(ADS_STATE_REQUEST | ADS_STATE_RESPONSE, ADS_STATE_REPLY);
}

// ReadDeviceInfo carries no payload, so its frame is exactly the preamble and every octet of it is
// dictated by the AMS layout:
//   00 00                       AMS/TCP reserved
//   20 00 00 00                 length = 32 (AMS header) + 0 (cbData), little-endian
//   05 12 03 04 01 01           target AMSNetId 5.18.3.4.1.1, in order
//   53 03                       target port 851 = 0x0353, little-endian
//   C0 A8 01 0A 01 01           source AMSNetId 192.168.1.10.1.1
//   E8 80                       source port 33000 = 0x80E8, little-endian
//   01 00                       cmd 1 = ReadDeviceInfo
//   04 00                       state flags = ADS_STATE_ADS_COMMAND, response bit clear
//   00 00 00 00                 cbData
//   00 00 00 00                 error code, zero on a request
//   78 56 34 12                 invoke id 0x12345678, little-endian
void test_ams_header_octet_layout(void)
{
    static const uint8_t WANT[ADS_HDR_LEN] = {
        0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x05, 0x12, 0x03, 0x04, 0x01, 0x01, 0x53,
        0x03, 0xC0, 0xA8, 0x01, 0x0A, 0x01, 0x01, 0xE8, 0x80, 0x01, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
    };
    AdsRequest r = route();
    uint8_t buf[64];
    memset(buf, 0xEE, sizeof(buf));
    size_t n = protocore_ads_build_read_device_info(buf, sizeof(buf), &r);
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, ADS_HDR_LEN);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[ADS_HDR_LEN]); // nothing written past the frame
}

// The AMS/TCP length field counts the AMS header plus cbData and excludes its own 6 octets, so for
// a Read (payload = IndexGroup + IndexOffset + Length = 12) it reads 32 + 12 = 44 = 0x2C.
void test_read_request_length_and_payload(void)
{
    AdsRequest r = route();
    uint8_t buf[64];
    size_t n = protocore_ads_build_read(buf, sizeof(buf), &r, ADS_IGRP_SYM_VAL_BY_HANDLE, 0x00010001u, 4);
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN + 12u, n);

    static const uint8_t LEN_LE[4] = {0x2C, 0x00, 0x00, 0x00}; // 44
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LEN_LE, buf + 2, 4);
    TEST_ASSERT_EQUAL_HEX8(0x02u, buf[22]); // cmd 2 = Read
    TEST_ASSERT_EQUAL_HEX8(0x00u, buf[23]);
    static const uint8_t CBDATA_LE[4] = {0x0C, 0x00, 0x00, 0x00}; // 12
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CBDATA_LE, buf + 26, 4);

    // payload: IndexGroup 0xF005, IndexOffset 0x00010001, Length 4, each 32-bit little-endian
    static const uint8_t PAYLOAD[12] = {0x05, 0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, buf + ADS_HDR_LEN, 12);
}

// The parser reads back what the builder laid down, target and source not swapped.
void test_header_round_trip(void)
{
    AdsRequest r = route();
    uint8_t buf[64];
    size_t n = protocore_ads_build_read_state(buf, sizeof(buf), &r);
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN, n);

    AdsAmsHeader h;
    TEST_ASSERT_TRUE(protocore_ads_parse_ams_header(buf, n, &h));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r.target.net_id, h.target.net_id, ADS_NET_ID_LEN);
    TEST_ASSERT_EQUAL_UINT16(851u, h.target.port);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r.source.net_id, h.source.net_id, ADS_NET_ID_LEN);
    TEST_ASSERT_EQUAL_UINT16(33000u, h.source.port);
    TEST_ASSERT_EQUAL_INT(ADS_COMMAND_READ_STATE, h.cmd);
    TEST_ASSERT_EQUAL_HEX16(ADS_STATE_REQUEST, h.state_flags);
    TEST_ASSERT_EQUAL_UINT32(0u, h.data_len);
    TEST_ASSERT_EQUAL_UINT32(0u, h.error_code);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, h.invoke_id);
    TEST_ASSERT_EQUAL_PTR(buf + ADS_HDR_LEN, h.data);
}

// A Write frame carries IndexGroup + IndexOffset + Length + Data, and cbData counts the data too.
void test_write_frames_the_data_after_the_length(void)
{
    AdsRequest r = route();
    static const uint8_t DATA[3] = {0xDE, 0xAD, 0xBE};
    uint8_t buf[64];
    size_t n = protocore_ads_build_write(buf, sizeof(buf), &r, ADS_IGRP_PLC_RW_M, 0x20u, DATA, sizeof(DATA));
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN + 12u + 3u, n);

    AdsAmsHeader h;
    TEST_ASSERT_TRUE(protocore_ads_parse_ams_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(ADS_COMMAND_WRITE, h.cmd);
    TEST_ASSERT_EQUAL_UINT32(15u, h.data_len); // 12 + 3

    // index group 0x4020 (%M flag memory), offset 0x20, length 3, then the data
    static const uint8_t PAYLOAD[15] = {0x20, 0x40, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
                                        0x03, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, h.data, 15);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_ads_build_write(buf, sizeof(buf), &r, 0, 0, NULL, 4)); // len without data
}

// ReadWrite is the symbol-by-name workhorse: write the symbol name to index group 0xF003 and read
// back the 4-octet handle. Its payload is IG + IO + ReadLen + WriteLen + WriteData = 16 + n.
void test_read_write_symbol_by_name(void)
{
    AdsRequest r = route();
    static const uint8_t NAME[8] = {'M', 'A', 'I', 'N', '.', 'v', 'a', 'l'};
    uint8_t buf[80];
    size_t n = protocore_ads_build_read_write(buf, sizeof(buf), &r, ADS_IGRP_SYM_HND_BY_NAME, 0, 4, NAME, sizeof(NAME));
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN + 16u + 8u, n);

    AdsAmsHeader h;
    TEST_ASSERT_TRUE(protocore_ads_parse_ams_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(ADS_COMMAND_READ_WRITE, h.cmd);
    TEST_ASSERT_EQUAL_UINT32(24u, h.data_len);

    static const uint8_t PAYLOAD[24] = {0x03, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
                                        0x08, 0x00, 0x00, 0x00, 'M',  'A',  'I',  'N',  '.',  'v',  'a',  'l'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, h.data, 24);
}

// WriteControl names the requested ADSSTATE and device state ahead of its data block.
void test_write_control_carries_the_state_pair(void)
{
    AdsRequest r = route();
    uint8_t buf[64];
    size_t n = protocore_ads_build_write_control(buf, sizeof(buf), &r, ADS_STATE_RUN, 0, NULL, 0);
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN + 8u, n);

    AdsAmsHeader h;
    TEST_ASSERT_TRUE(protocore_ads_parse_ams_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(ADS_COMMAND_WRITE_CONTROL, h.cmd);
    // AdsState 5 = Run, DeviceState 0, Length 0, all little-endian
    static const uint8_t PAYLOAD[8] = {0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, h.data, 8);
    TEST_ASSERT_EQUAL_INT(5, ADS_STATE_RUN);
}

// AddDeviceNotification's payload is 40 octets: six 32-bit fields plus 16 reserved zero octets.
void test_add_notification_payload_is_forty_octets(void)
{
    AdsRequest r = route();
    uint8_t buf[96];
    size_t n = protocore_ads_build_add_notification(buf, sizeof(buf), &r, ADS_IGRP_SYM_VAL_BY_HANDLE, 0x11u, 2,
                                                    ADS_TRANS_MODE_SERVER_ON_CHANGE, 0, 1000000);
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN + 40u, n);

    AdsAmsHeader h;
    TEST_ASSERT_TRUE(protocore_ads_parse_ams_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(ADS_COMMAND_ADD_NOTIFICATION, h.cmd);
    TEST_ASSERT_EQUAL_UINT32(40u, h.data_len);

    // 0xF005, offset 0x11, length 2, mode 4 (server on change), max delay 0,
    // cycle time 1000000 = 0x000F4240 -> 40 42 0F 00, then 16 reserved zeros.
    static const uint8_t PAYLOAD[40] = {
        0x05, 0xF0, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x42, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, h.data, 40);
    TEST_ASSERT_EQUAL_INT(4, ADS_TRANS_MODE_SERVER_ON_CHANGE);

    n = protocore_ads_build_del_notification(buf, sizeof(buf), &r, 0x0000002Au);
    TEST_ASSERT_EQUAL_size_t((size_t)ADS_HDR_LEN + 4u, n);
    TEST_ASSERT_TRUE(protocore_ads_parse_ams_header(buf, n, &h));
    TEST_ASSERT_EQUAL_INT(ADS_COMMAND_DEL_NOTIFICATION, h.cmd);
    static const uint8_t HANDLE[4] = {0x2A, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HANDLE, h.data, 4);
}

// Read / ReadWrite response: Result(4) + Length(4) + Data(Length), all little-endian.
void test_parse_read_response(void)
{
    static const uint8_t RESP[12] = {0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
    AdsReadResult res;
    TEST_ASSERT_TRUE(protocore_ads_parse_read(RESP, sizeof(RESP), &res));
    TEST_ASSERT_EQUAL_UINT32(0u, res.result);
    TEST_ASSERT_EQUAL_UINT32(4u, res.len);
    TEST_ASSERT_EQUAL_PTR(RESP + 8, res.data);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RESP + 8, res.data, 4);

    // An error result: ADS error 0x00000706 (invalid index group / symbol not found).
    static const uint8_t ERR[8] = {0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(protocore_ads_parse_read(ERR, sizeof(ERR), &res));
    TEST_ASSERT_EQUAL_HEX32(0x00000706u, res.result);
    TEST_ASSERT_EQUAL_UINT32(0u, res.len);

    // A Length that runs past the buffer is refused rather than pointing at nothing.
    static const uint8_t LIES[10] = {0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0xAA, 0xBB};
    TEST_ASSERT_FALSE(protocore_ads_parse_read(LIES, sizeof(LIES), &res));
    TEST_ASSERT_FALSE(protocore_ads_parse_read(RESP, 7, &res)); // shorter than Result + Length
}

// ReadState response: Result(4) + AdsState(2) + DeviceState(2).
void test_parse_read_state_response(void)
{
    static const uint8_t RESP[8] = {0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00};
    AdsReadStateResult st;
    TEST_ASSERT_TRUE(protocore_ads_parse_read_state(RESP, sizeof(RESP), &st));
    TEST_ASSERT_EQUAL_UINT32(0u, st.result);
    TEST_ASSERT_EQUAL_UINT16(ADS_STATE_RUN, st.protocore_ads_state);
    TEST_ASSERT_EQUAL_UINT16(1u, st.device_state);
    TEST_ASSERT_FALSE(protocore_ads_parse_read_state(RESP, 7, &st));

    uint32_t result = 0xFFFFFFFFu;
    TEST_ASSERT_TRUE(protocore_ads_parse_result(RESP, sizeof(RESP), &result));
    TEST_ASSERT_EQUAL_UINT32(0u, result);
    TEST_ASSERT_FALSE(protocore_ads_parse_result(RESP, 3, &result));
}

// ReadDeviceInfo response: Result(4) + Major(1) + Minor(1) + Build(2) + Name(16). The name field is
// a fixed 16 octets that need not be NUL-terminated, so the parser terminates its own copy.
void test_parse_device_info_terminates_a_full_name(void)
{
    static const uint8_t RESP[24] = {
        0x00, 0x00, 0x00, 0x00,                     // result
        0x03, 0x01, 0x0F, 0x10,                     // 3.1 build 0x100F = 4111
        'T',  'w',  'i',  'n',  'C', 'A', 'T', '3', // name, all 16 octets used
        'P',  'l',  'c',  'R',  'u', 'n', 't', 'm',
    };
    AdsDeviceInfo info;
    TEST_ASSERT_TRUE(protocore_ads_parse_read_device_info(RESP, sizeof(RESP), &info));
    TEST_ASSERT_EQUAL_UINT32(0u, info.result);
    TEST_ASSERT_EQUAL_UINT8(3u, info.version_major);
    TEST_ASSERT_EQUAL_UINT8(1u, info.version_minor);
    TEST_ASSERT_EQUAL_UINT16(4111u, info.version_build);
    TEST_ASSERT_EQUAL_STRING("TwinCAT3PlcRuntm", info.device_name);
    TEST_ASSERT_EQUAL_size_t(16u, strlen(info.device_name));

    TEST_ASSERT_FALSE(protocore_ads_parse_read_device_info(RESP, 23, &info));
}

// AddDeviceNotification response: Result(4) + NotificationHandle(4).
void test_parse_add_notification_response(void)
{
    static const uint8_t RESP[8] = {0x00, 0x00, 0x00, 0x00, 0x21, 0x43, 0x65, 0x87};
    uint32_t result = 1, handle = 0;
    TEST_ASSERT_TRUE(protocore_ads_parse_add_notification(RESP, sizeof(RESP), &result, &handle));
    TEST_ASSERT_EQUAL_UINT32(0u, result);
    TEST_ASSERT_EQUAL_HEX32(0x87654321u, handle);
    TEST_ASSERT_FALSE(protocore_ads_parse_add_notification(RESP, 7, &result, &handle));
}

// One DeviceNotification stamp carrying two samples, walked to both callbacks.
//
// The timestamp is a Windows FILETIME, 100 ns ticks since 1601-01-01 UTC. The value used here is
// the FILETIME of the Unix epoch, derived from the calendar alone:
//   1601-01-01 .. 1970-01-01 = 369 years = 369*365 = 134685 days,
//   plus leap days for 1604..1968 every 4th year = 92, less 1700/1800/1900 = 3, so +89
//                            = 134774 days
//   134774 * 86400 = 11644473600 s, * 10^7 ticks/s = 116444736000000000 = 0x019DB1DED53E8000
static uint32_t g_calls;
static uint32_t g_handles[4];
static uint64_t g_stamp;
static uint8_t g_first_sample;

static void on_sample(uint32_t handle, const uint8_t *sample, uint32_t sample_len, uint64_t timestamp, void *user)
{
    (void)user;
    if (g_calls < 4)
    {
        g_handles[g_calls] = handle;
    }
    if (g_calls == 0 && sample_len > 0)
    {
        g_first_sample = sample[0];
    }
    g_stamp = timestamp;
    g_calls++;
}

void test_walks_a_notification_stamp(void)
{
    static const uint8_t NOTE[] = {
        0x24, 0x00, 0x00, 0x00,                                     // Length = 36 octets after this field
        0x01, 0x00, 0x00, 0x00,                                     // Stamps = 1
        0x00, 0x80, 0x3E, 0xD5, 0xDE, 0xB1, 0x9D, 0x01,             // FILETIME 0x019DB1DED53E8000, little-endian
        0x02, 0x00, 0x00, 0x00,                                     // Samples = 2
        0x0A, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x11, 0x22, // handle 10, size 2
        0x0B, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x33, 0x44, // handle 11, size 2
    };
    g_calls = 0;
    g_stamp = 0;
    g_first_sample = 0;
    TEST_ASSERT_TRUE(protocore_ads_parse_notification(NOTE, sizeof(NOTE), on_sample, NULL));
    TEST_ASSERT_EQUAL_UINT32(2u, g_calls);
    TEST_ASSERT_EQUAL_UINT32(10u, g_handles[0]);
    TEST_ASSERT_EQUAL_UINT32(11u, g_handles[1]);
    TEST_ASSERT_EQUAL_HEX8(0x11u, g_first_sample);
    TEST_ASSERT_EQUAL_HEX64(0x019DB1DED53E8000ull, g_stamp);

    // A sample size that runs off the end aborts the walk instead of reading past the buffer.
    static const uint8_t TRUNC[] = {
        0x19, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // Length = 25, the octets that follow
        0x00, 0x80, 0x3E, 0xD5, 0xDE, 0xB1, 0x9D, 0x01, 0x01, 0x00, 0x00,
        0x00, 0x0A, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x11, // size 64, only 1 octet present
    };
    g_calls = 0;
    TEST_ASSERT_FALSE(protocore_ads_parse_notification(TRUNC, sizeof(TRUNC), on_sample, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, g_calls);
    TEST_ASSERT_FALSE(protocore_ads_parse_notification(NOTE, 7, on_sample, NULL));
}

// Framing that does not add up is refused: a nonzero AMS/TCP reserved field, a length shorter than
// the AMS header, a length promising more than arrived, and a cbData wider than the frame.
void test_malformed_framing_is_refused(void)
{
    AdsRequest r = route();
    uint8_t buf[64];
    size_t n = protocore_ads_build_read_state(buf, sizeof(buf), &r);
    AdsAmsHeader h;

    uint8_t bad[64];
    memcpy(bad, buf, n);
    bad[0] = 0x01; // reserved must be zero
    TEST_ASSERT_FALSE(protocore_ads_parse_ams_header(bad, n, &h));

    memcpy(bad, buf, n);
    bad[2] = 0x1F; // length 31 < the 32-octet AMS header
    TEST_ASSERT_FALSE(protocore_ads_parse_ams_header(bad, n, &h));

    memcpy(bad, buf, n);
    bad[2] = 0xFF; // length promises far more than arrived
    bad[3] = 0x00;
    TEST_ASSERT_FALSE(protocore_ads_parse_ams_header(bad, n, &h));

    memcpy(bad, buf, n);
    bad[26] = 0x40; // cbData 64 with a frame that carries none
    TEST_ASSERT_FALSE(protocore_ads_parse_ams_header(bad, n, &h));

    TEST_ASSERT_FALSE(protocore_ads_parse_ams_header(buf, (size_t)ADS_HDR_LEN - 1, &h));
    TEST_ASSERT_FALSE(protocore_ads_parse_ams_header(NULL, n, &h));
    TEST_ASSERT_FALSE(protocore_ads_parse_ams_header(buf, n, NULL));
}

// A buffer that cannot hold the whole frame yields 0 rather than a truncated one.
void test_builders_refuse_a_short_buffer(void)
{
    AdsRequest r = route();
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ads_build_read_device_info(buf, (size_t)ADS_HDR_LEN - 1, &r));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ads_build_read(buf, (size_t)ADS_HDR_LEN + 11u, &r, 0, 0, 4));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ads_build_read_device_info(NULL, sizeof(buf), &r));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_ads_build_read_device_info(buf, sizeof(buf), NULL));
}
