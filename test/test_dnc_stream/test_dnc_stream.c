// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the DNC drip-feed engine (services/machine_tool/dnc/dnc_stream): stream a G-code program over a
// send/recv seam and verify it round-trips through a scripted mock controller that decodes the wire
// bytes back to lines (ISO + EIA tape codes), that XON/XOFF flow control pauses and resumes the
// feed, and that an untranslatable line fails closed.

#include "services/machine_tool/dnc/dnc.h"
#include "services/machine_tool/dnc/dnc_stream.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A mock controller: mock_send decodes the streamed bytes back into lines + program markers;
// mock_recv is the reverse channel, optionally injecting an XOFF (then XON) once the feed passes a
// byte threshold, to exercise the pause/resume path.
typedef struct
{
    DncDecoder dec;
    char lines[16][PC_DNC_LINE_MAX + 1];
    int nlines;
    int prog_start, prog_end;
    size_t bytes_sent;
    // reverse-channel scripting
    proto_bool xoff_pending;
    size_t xoff_threshold;
    int xon_countdown;
    proto_bool paused_seen;
    proto_bool fail_send;
    proto_bool never_xon;            // once XOFF is delivered the controller never releases it
    proto_bool fail_recv_after_xoff; // the reverse channel breaks while the feed is paused
    int send_calls;
    int fail_send_at; // 1-based send-call index that reports an error (0 = never)
    long recv_calls;
    proto_bool fail_recv; // every reverse-channel poll reports an error
} MockCtrl;

static void mock_init(MockCtrl *m, DncCode code)
{
    memset(m, 0, sizeof(*m));
    pc_dnc_decode_init(&m->dec, code);
}

static int mock_send(void *c, const uint8_t *d, size_t len)
{
    MockCtrl *m = (MockCtrl *)c;
    m->send_calls++;
    if (m->fail_send || (m->fail_send_at && m->send_calls == m->fail_send_at))
    {
        return -1;
    }
    m->bytes_sent += len;
    for (size_t k = 0; k < len; k++)
    {
        DncEvent ev = pc_dnc_decode_feed(&m->dec, d[k]);
        if (ev == DNC_EV_LINE && m->nlines < 16)
        {
            strcpy(m->lines[m->nlines], m->dec.line);
            m->nlines++;
        }
        else if (ev == DNC_EV_PROG_START)
        {
            m->prog_start++;
        }
        else if (ev == DNC_EV_PROG_END)
        {
            m->prog_end++;
        }
    }
    return (int)len;
}

static int mock_recv(void *c, uint8_t *buf, size_t cap)
{
    MockCtrl *m = (MockCtrl *)c;
    if (cap == 0)
    {
        return 0;
    }
    m->recv_calls++;
    if (m->fail_recv || (m->fail_recv_after_xoff && m->paused_seen))
    {
        return -1;
    }
    if (m->xoff_pending && m->bytes_sent >= m->xoff_threshold)
    {
        m->xoff_pending = PROTO_FALSE;
        m->paused_seen = PROTO_TRUE;
        if (!m->never_xon)
        {
            m->xon_countdown = 3;
        }
        buf[0] = (uint8_t)DNC_XOFF;
        return 1;
    }
    if (m->xon_countdown > 0)
    {
        if (--m->xon_countdown == 0)
        {
            buf[0] = (uint8_t)DNC_XON;
            return 1;
        }
        return 0; // an empty poll tick while "paused"
    }
    return 0;
}

static DncCfg iso_cfg()
{
    DncCfg c;
    memset(&c, 0, sizeof(c));
    c.code = DNC_CODE_ISO;
    return c;
}

void test_iso_roundtrip()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    DncCfg cfg = iso_cfg();
    const char *prog = "N10 G0 X1 Y2\nN20 G1 X3 F100\nM30";

    TEST_ASSERT_EQUAL_INT(DNC_STREAM_OK, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(1, m.prog_start);
    TEST_ASSERT_EQUAL_INT(1, m.prog_end);
    TEST_ASSERT_EQUAL_INT(3, m.nlines);
    TEST_ASSERT_EQUAL_STRING("N10 G0 X1 Y2", m.lines[0]);
    TEST_ASSERT_EQUAL_STRING("N20 G1 X3 F100", m.lines[1]);
    TEST_ASSERT_EQUAL_STRING("M30", m.lines[2]);
}

void test_eia_roundtrip()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_EIA);
    DncCfg cfg = iso_cfg();
    cfg.code = DNC_CODE_EIA;
    const char *prog = "N10 G0 X1\nN20 M30"; // uppercase + digits only (EIA has no lowercase)

    TEST_ASSERT_EQUAL_INT(DNC_STREAM_OK, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(1, m.prog_start);
    TEST_ASSERT_EQUAL_INT(1, m.prog_end);
    TEST_ASSERT_EQUAL_INT(2, m.nlines);
    TEST_ASSERT_EQUAL_STRING("N10 G0 X1", m.lines[0]);
    TEST_ASSERT_EQUAL_STRING("N20 M30", m.lines[1]);
}

void test_crlf_and_parity()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    DncCfg cfg = iso_cfg();
    cfg.crlf = PROTO_TRUE;
    cfg.even_parity = PROTO_TRUE;
    const char *prog = "G90\nG0 X0"; // decoder strips parity + the CR

    TEST_ASSERT_EQUAL_INT(DNC_STREAM_OK, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(2, m.nlines);
    TEST_ASSERT_EQUAL_STRING("G90", m.lines[0]);
    TEST_ASSERT_EQUAL_STRING("G0 X0", m.lines[1]);
}

void test_xoff_pacing()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    m.xoff_pending = PROTO_TRUE;
    m.xoff_threshold = 8; // pause partway through
    DncCfg cfg = iso_cfg();
    const char *prog = "N10 G0 X1\nN20 G1 X2\nN30 M30";

    TEST_ASSERT_EQUAL_INT(DNC_STREAM_OK, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(m.paused_seen); // the engine actually paused on XOFF
    TEST_ASSERT_EQUAL_INT(3, m.nlines);
    TEST_ASSERT_EQUAL_STRING("N30 M30", m.lines[2]); // and resumed to completion
}

void test_leader_trailer()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    DncCfg cfg = iso_cfg();
    cfg.leader_len = 8;
    const char *prog = "M30";

    size_t plen = strlen(prog);
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_OK, dnc_stream(&cfg, prog, plen, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(1, m.nlines);
    TEST_ASSERT_EQUAL_STRING("M30", m.lines[0]);
    // 8 leader + 8 trailer NUL runout bytes were emitted (skipped by the decoder)
    TEST_ASSERT_GREATER_OR_EQUAL(16u, m.bytes_sent);
}

void test_empty_program()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    DncCfg cfg = iso_cfg();
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_OK, dnc_stream(&cfg, "", 0, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(1, m.prog_start);
    TEST_ASSERT_EQUAL_INT(1, m.prog_end);
    TEST_ASSERT_EQUAL_INT(0, m.nlines);
}

void test_encode_error()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_EIA);
    DncCfg cfg = iso_cfg();
    cfg.code = DNC_CODE_EIA;
    const char *prog = "N10 x1"; // lowercase 'x' has no EIA representation -> fail closed
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_ENCODE, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &m));
}

void test_io_error_and_args()
{
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    m.fail_send = PROTO_TRUE;
    DncCfg cfg = iso_cfg();
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, "M30", 3, mock_send, mock_recv, &m));

    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_ARG, dnc_stream(NULL, "M30", 3, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_ARG, dnc_stream(&cfg, NULL, 3, mock_send, mock_recv, &m));
}

void test_null_send_or_recv_rejected()
{
    // Both halves of the seam are required: there is no "send-only" drip feed (the engine must be able
    // to read XON/XOFF back).
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    DncCfg cfg = iso_cfg();
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_ARG, dnc_stream(&cfg, "M30", 3, NULL, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_ARG, dnc_stream(&cfg, "M30", 3, mock_send, NULL, &m));
    TEST_ASSERT_EQUAL_INT(0, m.send_calls); // nothing was put on the wire
}

void test_reverse_channel_error_fails_the_stream()
{
    // A recv error is not "no bytes available": the engine cannot know the controller's flow state any
    // more, so it stops rather than blindly feeding a machine that may have asserted XOFF.
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    m.fail_recv = PROTO_TRUE;
    DncCfg cfg = iso_cfg();
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, "M30", 3, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.send_calls); // the very first drain failed, before any byte went out
}

void test_xoff_never_released_gives_up()
{
    // A controller that asserts XOFF and never releases it must not hang the feed forever.
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    m.xoff_pending = PROTO_TRUE;
    m.xoff_threshold = 0; // pause before the first block
    m.never_xon = PROTO_TRUE;
    DncCfg cfg = iso_cfg();
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, "M30", 3, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(m.paused_seen);
    TEST_ASSERT_TRUE(m.recv_calls > PC_DNC_XOFF_MAX_POLLS); // it really did poll to the bound
}

void test_reverse_channel_error_while_paused()
{
    // The reverse channel breaking mid-pause is an error, not an implicit XON.
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    m.xoff_pending = PROTO_TRUE;
    m.xoff_threshold = 0;
    m.never_xon = PROTO_TRUE;
    m.fail_recv_after_xoff = PROTO_TRUE;
    DncCfg cfg = iso_cfg();
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, "M30", 3, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(m.paused_seen);
    TEST_ASSERT_TRUE(m.recv_calls < PC_DNC_XOFF_MAX_POLLS); // it bailed on the error, not on the poll bound
}

void test_send_failure_at_each_stage()
{
    DncCfg cfg = iso_cfg();
    const char *prog = "M30";

    // Stage 1 of a leadered stream is the leader runout itself.
    MockCtrl leader;
    mock_init(&leader, DNC_CODE_ISO);
    cfg.leader_len = 8;
    leader.fail_send_at = 1;
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &leader));
    TEST_ASSERT_EQUAL_INT(1, leader.send_calls); // it stopped at the first write

    // Without a leader the stages are: start marker, one block per line, end marker.
    cfg.leader_len = 0;
    MockCtrl block;
    mock_init(&block, DNC_CODE_ISO);
    block.fail_send_at = 2;
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &block));
    TEST_ASSERT_EQUAL_INT(1, block.prog_start); // the marker landed, the block did not
    TEST_ASSERT_EQUAL_INT(0, block.nlines);

    MockCtrl endmark;
    mock_init(&endmark, DNC_CODE_ISO);
    endmark.fail_send_at = 3;
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &endmark));
    TEST_ASSERT_EQUAL_INT(1, endmark.nlines);
    TEST_ASSERT_EQUAL_INT(0, endmark.prog_end); // the closing marker never went out

    // With a leader the trailer runout is the fifth and last write.
    cfg.leader_len = 8;
    MockCtrl trailer;
    mock_init(&trailer, DNC_CODE_ISO);
    trailer.fail_send_at = 5;
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_ERR_IO, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &trailer));
    TEST_ASSERT_EQUAL_INT(1, trailer.prog_start);
    TEST_ASSERT_EQUAL_INT(1, trailer.prog_end); // the program itself completed; only the trailer failed
    TEST_ASSERT_EQUAL_INT(5, trailer.send_calls);
}

void test_blank_lines_and_crlf_source()
{
    // A CRLF source has its CR stripped, and a blank line produces an empty block (no decoded line).
    MockCtrl m;
    mock_init(&m, DNC_CODE_ISO);
    DncCfg cfg = iso_cfg();
    const char *prog = "G90\r\nG0 X0\n\nM30";
    TEST_ASSERT_EQUAL_INT(DNC_STREAM_OK, dnc_stream(&cfg, prog, strlen(prog), mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(3, m.nlines);
    TEST_ASSERT_EQUAL_STRING("G90", m.lines[0]); // the CR did not survive into the block
    TEST_ASSERT_EQUAL_STRING("G0 X0", m.lines[1]);
    TEST_ASSERT_EQUAL_STRING("M30", m.lines[2]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_iso_roundtrip);
    RUN_TEST(test_eia_roundtrip);
    RUN_TEST(test_crlf_and_parity);
    RUN_TEST(test_xoff_pacing);
    RUN_TEST(test_leader_trailer);
    RUN_TEST(test_empty_program);
    RUN_TEST(test_encode_error);
    RUN_TEST(test_io_error_and_args);
    RUN_TEST(test_null_send_or_recv_rejected);
    RUN_TEST(test_reverse_channel_error_fails_the_stream);
    RUN_TEST(test_xoff_never_released_gives_up);
    RUN_TEST(test_reverse_channel_error_while_paused);
    RUN_TEST(test_send_failure_at_each_stage);
    RUN_TEST(test_blank_lines_and_crlf_source);
    return UNITY_END();
}
