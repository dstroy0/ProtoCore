// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the QUIC frame codec (network_drivers/presentation/http/http3/pc_quic_frame, RFC 9000
// sec 19): builder/parser round-trips for PADDING/PING/HANDSHAKE_DONE, ACK (single-range built +
// a hand-built multi-range-with-ECN cursor advance), CRYPTO, STREAM (with/without Offset, and the
// LEN-absent "runs to packet end" case), MAX_DATA, and CONNECTION_CLOSE (transport + app), plus a
// multi-frame sequential parse and truncation rejection. Pure host codec.

#include "network_drivers/presentation/http/http3/quic_frame.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_simple_frames()
{
    uint8_t b[4];
    QuicFrame f;
    TEST_ASSERT_EQUAL_INT(1, (int)pc_quic_build_ping(b, sizeof b));
    TEST_ASSERT_EQUAL_INT(1, (int)pc_quic_frame_parse(b, 1, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_PING, (unsigned)f.type);

    TEST_ASSERT_EQUAL_INT(1, (int)pc_quic_build_handshake_done(b, sizeof b));
    TEST_ASSERT_EQUAL_INT(1, (int)pc_quic_frame_parse(b, 1, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_HANDSHAKE_DONE, (unsigned)f.type);

    // A PADDING byte (0x00) parses as one PADDING frame consuming one byte.
    const uint8_t pad[1] = {0x00};
    TEST_ASSERT_EQUAL_INT(1, (int)pc_quic_frame_parse(pad, 1, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_PADDING, (unsigned)f.type);
}

void test_ack()
{
    uint8_t b[16];
    size_t n = pc_quic_build_ack(b, sizeof b, 1000, 42, 3);
    TEST_ASSERT_TRUE(n > 0);
    QuicFrame f;
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(b, n, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_ACK, (unsigned)f.type);
    TEST_ASSERT_TRUE(f.ack.largest == 1000 && f.ack.delay == 42);
    TEST_ASSERT_TRUE(f.ack.range_count == 0 && f.ack.first_range == 3);

    // Hand-built ACK w/ ECN (0x03): largest 60, delay 5, 1 range (gap 2, len 4), ECN 1/2/0.
    const uint8_t ecn[10] = {0x03, 60, 5, 1, 3, 2, 4, 1, 2, 0};
    TEST_ASSERT_EQUAL_INT(10, (int)pc_quic_frame_parse(ecn, sizeof ecn, &f));
    TEST_ASSERT_TRUE(f.ack.largest == 60 && f.ack.range_count == 1 && f.ack.first_range == 3);
}

void test_crypto()
{
    uint8_t b[32];
    const uint8_t data[5] = {'h', 'e', 'l', 'l', 'o'};
    size_t n = pc_quic_build_crypto(b, sizeof b, 7, data, 5);
    TEST_ASSERT_TRUE(n > 0);
    QuicFrame f;
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(b, n, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_CRYPTO, (unsigned)f.type);
    TEST_ASSERT_TRUE(f.crypto.offset == 7 && f.crypto.length == 5);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, f.crypto.data, 5);
}

void test_stream()
{
    uint8_t b[32];
    const uint8_t data[3] = {1, 2, 3};
    // With offset + FIN.
    size_t n = pc_quic_build_stream(b, sizeof b, 4, 100, data, 3, PROTO_TRUE);
    QuicFrame f;
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(b, n, &f));
    TEST_ASSERT_TRUE((f.type & 0xf8) == QUIC_FT_STREAM);
    TEST_ASSERT_TRUE(f.stream.id == 4 && f.stream.offset == 100 && f.stream.length == 3 && f.stream.fin == 1);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, f.stream.data, 3);

    // No offset, no FIN.
    n = pc_quic_build_stream(b, sizeof b, 0, 0, data, 3, PROTO_FALSE);
    TEST_ASSERT_FALSE(b[0] & QUIC_STREAM_OFF);
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(b, n, &f));
    TEST_ASSERT_TRUE(f.stream.id == 0 && f.stream.offset == 0 && f.stream.fin == 0);

    // LEN bit clear -> Stream Data runs to the end of the packet. Type 0x08 (STREAM), id 0.
    const uint8_t to_end[5] = {0x08, 0x00, 0xAA, 0xBB, 0xCC};
    TEST_ASSERT_EQUAL_INT(5, (int)pc_quic_frame_parse(to_end, sizeof to_end, &f));
    TEST_ASSERT_TRUE(f.stream.id == 0 && f.stream.length == 3);
    TEST_ASSERT_EQUAL_HEX8(0xAA, f.stream.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, f.stream.data[2]);
}

void test_max_data_and_close()
{
    uint8_t b[64];
    size_t n = pc_quic_build_max_data(b, sizeof b, 65536);
    QuicFrame f;
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(b, n, &f));
    TEST_ASSERT_TRUE(f.type == QUIC_FT_MAX_DATA && f.max_data.max == 65536);

    n = pc_quic_build_connection_close(b, sizeof b, 0x0a, QUIC_FT_STREAM, "bad", 3);
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(b, n, &f));
    TEST_ASSERT_TRUE(f.type == QUIC_FT_CONNECTION_CLOSE && f.close.error_code == 0x0a);
    TEST_ASSERT_TRUE(f.close.frame_type == QUIC_FT_STREAM && f.close.reason_len == 3 && f.close.app == 0);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("bad", f.close.reason, 3);

    // Application-level close (0x1d) has no triggering frame type.
    const uint8_t appclose[4] = {0x1d, 0x05, 0x01, 'x'};
    TEST_ASSERT_EQUAL_INT(4, (int)pc_quic_frame_parse(appclose, sizeof appclose, &f));
    TEST_ASSERT_TRUE(f.close.app == 1 && f.close.error_code == 5 && f.close.frame_type == 0 && f.close.reason_len == 1);
}

void test_sequence_and_truncation()
{
    // A packet payload: PADDING, PING, then a CRYPTO frame - parse them in order.
    uint8_t buf[32];
    size_t o = 0;
    buf[o++] = QUIC_FT_PADDING;
    buf[o++] = QUIC_FT_PING;
    const uint8_t data[2] = {0xDE, 0xAD};
    o += pc_quic_build_crypto(buf + o, sizeof buf - o, 0, data, 2);

    size_t pos = 0;
    QuicFrame f;
    size_t c = pc_quic_frame_parse(buf + pos, o - pos, &f);
    TEST_ASSERT_TRUE(c == 1 && f.type == QUIC_FT_PADDING);
    pos += c;
    c = pc_quic_frame_parse(buf + pos, o - pos, &f);
    TEST_ASSERT_TRUE(c == 1 && f.type == QUIC_FT_PING);
    pos += c;
    c = pc_quic_frame_parse(buf + pos, o - pos, &f);
    TEST_ASSERT_TRUE(c > 0 && f.type == QUIC_FT_CRYPTO && f.crypto.length == 2);
    pos += c;
    TEST_ASSERT_EQUAL_UINT((unsigned)o, (unsigned)pos);

    // A CRYPTO frame whose Length exceeds the buffer must be rejected.
    const uint8_t bad[3] = {QUIC_FT_CRYPTO, 0x00, 0x10}; // offset 0, length 16, but no data
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(bad, sizeof bad, &f));
}

// Every builder returns 0 when the output buffer is too small (each guard exercised).
void test_builder_overflow()
{
    uint8_t b[1];
    const uint8_t d[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_ping(b, 0));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_handshake_done(b, 0));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_padding(b, 1, 3)); // n > cap
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_ack(b, 1, 1000, 42, 3));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_crypto(b, 1, 7, d, 4)); // header fits, data does not
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_crypto(b, 0, 0, d, 4)); // type varint does not fit
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_stream(b, 1, 4, 100, d, 4, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_stream(b, 0, 0, 0, d, 4, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_max_data(b, 1, 1u << 30));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_connection_close(b, 1, 0x0a, 0, "x", 1));
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_connection_close(b, 4, 0x0a, 0, "hello", 5)); // reason overflows
}

// Every parse guard: empty input, per-frame truncation, and an unhandled frame type.
void test_parse_errors()
{
    QuicFrame f;
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse((const uint8_t *)"", 0, &f)); // no type byte

    const uint8_t ack_trunc[1] = {QUIC_FT_ACK}; // no Largest/Delay/...
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(ack_trunc, 1, &f));
    const uint8_t ack_ranges_trunc[5] = {0x02, 60, 5, 1, 3}; // range_count 1 but no Gap/Length
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(ack_ranges_trunc, 5, &f));
    const uint8_t ack_ecn_trunc[7] = {0x03, 60, 5, 0, 3, 1, 2}; // ECN needs 3 counts, only 2
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(ack_ecn_trunc, 7, &f));

    const uint8_t crypto_trunc[2] = {QUIC_FT_CRYPTO, 0x00}; // offset ok, no length
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(crypto_trunc, 2, &f));

    const uint8_t stream_noid[1] = {0x08}; // STREAM, no Stream ID
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(stream_noid, 1, &f));
    const uint8_t stream_off_trunc[2] = {0x0c, 0x00}; // OFF set, id ok, no Offset
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(stream_off_trunc, 2, &f));
    const uint8_t stream_len_over[3] = {0x0a, 0x00, 0x08}; // LEN set, length 8 > remaining
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(stream_len_over, 3, &f));

    const uint8_t max_trunc[1] = {QUIC_FT_MAX_DATA};
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(max_trunc, 1, &f));

    const uint8_t close_trunc[1] = {QUIC_FT_CONNECTION_CLOSE};
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(close_trunc, 1, &f));
    const uint8_t close_reason_over[4] = {0x1c, 0x00, 0x00, 0x08}; // reason len 8 > remaining
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(close_reason_over, 4, &f));
    const uint8_t appclose_trunc[2] = {0x1d, 0x00}; // error code ok, no reason length
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(appclose_trunc, 2, &f));

    const uint8_t unhandled[1] = {0x18}; // NEW_CONNECTION_ID - not handled by this minimal server
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(unhandled, 1, &f));
}

// Remaining per-field truncations (STREAM Length varint, transport CONNECTION_CLOSE
// frame-type varint) and builder mid-write overflow guards (padding success; crypto/stream
// header fits but the body/offset/length does not).
void test_frame_edge_guards()
{
    QuicFrame f;
    // STREAM with LEN set but the Length varint is absent -> rejected at the length read.
    const uint8_t stream_len_trunc[2] = {0x0a, 0x00}; // STREAM|LEN, id 0, no Length
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(stream_len_trunc, 2, &f));
    // Transport CONNECTION_CLOSE with an error code but no triggering-frame-type varint.
    const uint8_t close_ft_trunc[2] = {0x1c, 0x00};
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(close_ft_trunc, 2, &f));

    uint8_t b[8];
    const uint8_t d[3] = {1, 2, 3};
    // pc_quic_build_padding success: n <= cap zeroes n bytes and returns n.
    TEST_ASSERT_EQUAL_INT(3, (int)pc_quic_build_padding(b, sizeof b, 3));
    TEST_ASSERT_EQUAL_HEX8(0, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0, b[2]);
    // CRYPTO: type+offset+length varints fit but the data does not.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_crypto(b, 4, 7, d, 3));
    // STREAM: type+id fit but the Offset varint does not.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_stream(b, 2, 4, 100, d, 3, PROTO_TRUE));
    // STREAM (no offset): type+id fit but the Length varint does not.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_stream(b, 2, 0, 0, d, 3, PROTO_FALSE));
    // STREAM: the header fits but the stream data does not.
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_stream(b, 3, 0, 0, d, 3, PROTO_FALSE));
}

// An ACK (with ECN) carrying two ACK Ranges: exercises the range-skip loop across more than one
// iteration and the three ECN-count skips (the single-range test_ack iterates them at most once).
void test_ack_multi_range()
{
    // type 0x03, largest 60, delay 5, range_count 2, first_range 3, [gap 2,len 4][gap 1,len 1], ECN 1/2/0.
    const uint8_t ecn2[12] = {0x03, 60, 5, 2, 3, 2, 4, 1, 1, 1, 2, 0};
    QuicFrame f;
    TEST_ASSERT_EQUAL_INT(12, (int)pc_quic_frame_parse(ecn2, sizeof ecn2, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_ACK_ECN, (unsigned)f.type);
    TEST_ASSERT_TRUE(f.ack.largest == 60 && f.ack.range_count == 2 && f.ack.first_range == 3);
}

// Frames the minimal server does not act on but MUST still parse to skip (RFC 9000 sec 12.4): each
// wire-shape group, a valid instance (whole frame consumed) plus a truncation that must be rejected.
void test_skip_and_extra_frames()
{
    QuicFrame f;

    // One-varint frames: type followed by a single varint.
    const uint8_t one_varint[] = {QUIC_FT_MAX_STREAMS_BIDI,    QUIC_FT_MAX_STREAMS_UNI,
                                  QUIC_FT_DATA_BLOCKED,        QUIC_FT_STREAMS_BLOCKED_BIDI,
                                  QUIC_FT_STREAMS_BLOCKED_UNI, QUIC_FT_RETIRE_CONNECTION_ID};
    for (size_t i = 0; i < sizeof one_varint; i++)
    {
        const uint8_t ok[2] = {one_varint[i], 0x0a};
        TEST_ASSERT_EQUAL_INT(2, (int)pc_quic_frame_parse(ok, sizeof ok, &f));
        TEST_ASSERT_EQUAL_UINT(one_varint[i], (unsigned)f.type);
        const uint8_t trunc[1] = {one_varint[i]}; // the lone varint is absent
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(trunc, sizeof trunc, &f));
    }

    // Two-varint frames.
    const uint8_t two_varint[] = {QUIC_FT_STOP_SENDING, QUIC_FT_MAX_STREAM_DATA, QUIC_FT_STREAM_DATA_BLOCKED};
    for (size_t i = 0; i < sizeof two_varint; i++)
    {
        const uint8_t ok[3] = {two_varint[i], 0x01, 0x02};
        TEST_ASSERT_EQUAL_INT(3, (int)pc_quic_frame_parse(ok, sizeof ok, &f));
        TEST_ASSERT_EQUAL_UINT(two_varint[i], (unsigned)f.type);
        const uint8_t trunc[2] = {two_varint[i], 0x01}; // only the first varint present
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(trunc, sizeof trunc, &f));
    }

    // RESET_STREAM: three varints (stream id, app error code, final size).
    const uint8_t reset[4] = {QUIC_FT_RESET_STREAM, 0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_INT(4, (int)pc_quic_frame_parse(reset, sizeof reset, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_RESET_STREAM, (unsigned)f.type);
    const uint8_t reset_trunc[3] = {QUIC_FT_RESET_STREAM, 0x01, 0x02}; // third varint absent
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(reset_trunc, sizeof reset_trunc, &f));

    // NEW_TOKEN: token length + token bytes.
    const uint8_t new_token[5] = {QUIC_FT_NEW_TOKEN, 0x03, 0xAA, 0xBB, 0xCC};
    TEST_ASSERT_EQUAL_INT(5, (int)pc_quic_frame_parse(new_token, sizeof new_token, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_NEW_TOKEN, (unsigned)f.type);
    const uint8_t new_token_no_len[1] = {QUIC_FT_NEW_TOKEN}; // length varint absent
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(new_token_no_len, sizeof new_token_no_len, &f));
    const uint8_t new_token_over[3] = {QUIC_FT_NEW_TOKEN, 0x05, 0xAA}; // token len 5 > remaining
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(new_token_over, sizeof new_token_over, &f));

    // NEW_CONNECTION_ID: seq, retire-prior-to, 1-byte CID length, CID, 16-byte stateless reset token.
    uint8_t ncid[24];
    memset(ncid, 0, sizeof ncid);
    ncid[0] = QUIC_FT_NEW_CONNECTION_ID;
    ncid[1] = 0x01; // sequence number
    ncid[2] = 0x00; // retire prior to
    ncid[3] = 0x04; // CID length 4, then 4 CID + 16 token bytes = total 24
    TEST_ASSERT_EQUAL_INT(24, (int)pc_quic_frame_parse(ncid, sizeof ncid, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_NEW_CONNECTION_ID, (unsigned)f.type);
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(ncid, sizeof ncid - 1, &f)); // one byte short of CID + token
    const uint8_t ncid_hdr_only[3] = {QUIC_FT_NEW_CONNECTION_ID, 0x01, 0x00};      // no CID length byte
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(ncid_hdr_only, sizeof ncid_hdr_only, &f));

    // PATH_CHALLENGE / PATH_RESPONSE: 8 opaque bytes.
    uint8_t path[9];
    memset(path, 0x11, sizeof path);
    path[0] = QUIC_FT_PATH_CHALLENGE;
    TEST_ASSERT_EQUAL_INT(9, (int)pc_quic_frame_parse(path, sizeof path, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_PATH_CHALLENGE, (unsigned)f.type);
    path[0] = QUIC_FT_PATH_RESPONSE;
    TEST_ASSERT_EQUAL_INT(9, (int)pc_quic_frame_parse(path, sizeof path, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_PATH_RESPONSE, (unsigned)f.type);
    const uint8_t path_trunc[4] = {QUIC_FT_PATH_CHALLENGE, 0, 0, 0}; // fewer than 8 opaque bytes
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(path_trunc, sizeof path_trunc, &f));

    // A genuinely unknown / reserved frame type falls through to the final reject.
    const uint8_t unknown[1] = {0x1f};
    TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(unknown, sizeof unknown, &f));
}

// Every proper prefix of @p buf must be rejected: each successive varint / field read in the
// parser runs off the end at exactly one of these lengths, so this sweeps all of them at once.
static void reject_every_prefix(const uint8_t *buf, size_t n)
{
    QuicFrame f;
    for (size_t k = 1; k < n; k++)
    {
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_frame_parse(buf, k, &f));
    }
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(buf, n, &f)); // the whole frame parses
}

// Truncation at EVERY byte boundary of each multi-field frame: the ACK range/ECN loops, CRYPTO,
// RESET_STREAM, the two-varint group, NEW_CONNECTION_ID and PATH_CHALLENGE. Each prefix must be
// refused (0), and the complete frame must still parse.
void test_frame_truncation_sweep()
{
    // ACK with ECN: largest 60, delay 5, 2 ranges (gap/len pairs), then the three ECN counts.
    const uint8_t ack_ecn[12] = {0x03, 60, 5, 2, 3, 2, 4, 1, 1, 1, 2, 0};
    reject_every_prefix(ack_ecn, sizeof ack_ecn);

    // Plain ACK, no ranges: largest 1000 (2-octet varint), delay 42, 0 ranges, first_range 3.
    uint8_t ack[16];
    size_t an = pc_quic_build_ack(ack, sizeof ack, 1000, 42, 3);
    TEST_ASSERT_TRUE(an > 0);
    reject_every_prefix(ack, an);

    // CRYPTO with a multi-octet offset and 5 bytes of data.
    uint8_t cr[32];
    const uint8_t data[5] = {'h', 'e', 'l', 'l', 'o'};
    size_t cn = pc_quic_build_crypto(cr, sizeof cr, 1000, data, sizeof data);
    TEST_ASSERT_TRUE(cn > 0);
    reject_every_prefix(cr, cn);

    // RESET_STREAM: three varints, each multi-octet so every prefix cuts one short.
    const uint8_t reset[7] = {QUIC_FT_RESET_STREAM, 0x44, 0x00, 0x44, 0x01, 0x44, 0x02};
    reject_every_prefix(reset, sizeof reset);

    // STOP_SENDING: two varints.
    const uint8_t stop[5] = {QUIC_FT_STOP_SENDING, 0x44, 0x00, 0x44, 0x01};
    reject_every_prefix(stop, sizeof stop);

    // NEW_CONNECTION_ID: seq, retire-prior-to, 1-byte CID length, CID, 16-byte reset token.
    uint8_t ncid[24];
    memset(ncid, 0x5A, sizeof ncid);
    ncid[0] = QUIC_FT_NEW_CONNECTION_ID;
    ncid[1] = 0x01;
    ncid[2] = 0x00;
    ncid[3] = 0x04;
    reject_every_prefix(ncid, sizeof ncid);

    // PATH_CHALLENGE: 8 opaque bytes.
    uint8_t path[9];
    memset(path, 0x11, sizeof path);
    path[0] = QUIC_FT_PATH_CHALLENGE;
    reject_every_prefix(path, sizeof path);
}

// Every builder refuses every output capacity below the one it needs, and succeeds at exactly that
// size - sweeping each internal varint write's overflow guard in turn.
void test_builder_capacity_sweep()
{
    uint8_t out[64];
    const uint8_t data[5] = {1, 2, 3, 4, 5};

    // ACK: type + largest(2 octets) + delay(2) + range count + first range.
    size_t need = pc_quic_build_ack(out, sizeof out, 1000, 500, 3);
    TEST_ASSERT_TRUE(need > 0);
    for (size_t cap = 0; cap < need; cap++)
    {
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_ack(out, cap, 1000, 500, 3));
    }
    TEST_ASSERT_EQUAL_INT((int)need, (int)pc_quic_build_ack(out, need, 1000, 500, 3));

    // CRYPTO with a multi-octet offset.
    need = pc_quic_build_crypto(out, sizeof out, 1000, data, sizeof data);
    TEST_ASSERT_TRUE(need > 0);
    for (size_t cap = 0; cap < need; cap++)
    {
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_crypto(out, cap, 1000, data, sizeof data));
    }

    // STREAM with an offset and FIN.
    need = pc_quic_build_stream(out, sizeof out, 1000, 2000, data, sizeof data, PROTO_TRUE);
    TEST_ASSERT_TRUE(need > 0);
    for (size_t cap = 0; cap < need; cap++)
    {
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_stream(out, cap, 1000, 2000, data, sizeof data, PROTO_TRUE));
    }

    // MAX_DATA.
    need = pc_quic_build_max_data(out, sizeof out, 1000000);
    TEST_ASSERT_TRUE(need > 0);
    for (size_t cap = 0; cap < need; cap++)
    {
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_max_data(out, cap, 1000000));
    }

    // CONNECTION_CLOSE with a multi-octet error code, frame type and reason.
    need = pc_quic_build_connection_close(out, sizeof out, 1000, 2000, "reason", 6);
    TEST_ASSERT_TRUE(need > 0);
    for (size_t cap = 0; cap < need; cap++)
    {
        TEST_ASSERT_EQUAL_INT(0, (int)pc_quic_build_connection_close(out, cap, 1000, 2000, "reason", 6));
    }
}

// The zero-length bodies each builder must still emit correctly: an empty CRYPTO frame, an empty
// STREAM frame, and a CONNECTION_CLOSE with no reason phrase - all round-trip through the parser.
void test_builders_with_empty_bodies()
{
    uint8_t out[32];
    QuicFrame f;

    // CRYPTO carrying no data: header only, and it parses back with length 0.
    size_t n = pc_quic_build_crypto(out, sizeof out, 4, NULL, 0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(out, n, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_CRYPTO, (unsigned)f.type);
    TEST_ASSERT_EQUAL_UINT64(4, f.crypto.offset);
    TEST_ASSERT_EQUAL_UINT64(0, f.crypto.length);

    // STREAM carrying no data but a FIN: the pure-FIN frame the engine sends to end a stream.
    n = pc_quic_build_stream(out, sizeof out, 8, 0, NULL, 0, PROTO_TRUE);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(out, n, &f));
    TEST_ASSERT_EQUAL_UINT64(8, f.stream.id);
    TEST_ASSERT_EQUAL_UINT64(0, f.stream.length);
    TEST_ASSERT_EQUAL_UINT8(1, f.stream.fin);

    // CONNECTION_CLOSE with no reason phrase (what the engine emits).
    n = pc_quic_build_connection_close(out, sizeof out, 0x0a, 0, NULL, 0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT((int)n, (int)pc_quic_frame_parse(out, n, &f));
    TEST_ASSERT_EQUAL_UINT(QUIC_FT_CONNECTION_CLOSE, (unsigned)f.type);
    TEST_ASSERT_EQUAL_UINT64(0x0a, f.close.error_code);
    TEST_ASSERT_EQUAL_UINT64(0, f.close.reason_len);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_frame_edge_guards);
    RUN_TEST(test_frame_truncation_sweep);
    RUN_TEST(test_builder_capacity_sweep);
    RUN_TEST(test_builders_with_empty_bodies);
    RUN_TEST(test_simple_frames);
    RUN_TEST(test_ack);
    RUN_TEST(test_ack_multi_range);
    RUN_TEST(test_crypto);
    RUN_TEST(test_stream);
    RUN_TEST(test_max_data_and_close);
    RUN_TEST(test_sequence_and_truncation);
    RUN_TEST(test_builder_overflow);
    RUN_TEST(test_parse_errors);
    RUN_TEST(test_skip_and_extra_frames);
    return UNITY_END();
}
