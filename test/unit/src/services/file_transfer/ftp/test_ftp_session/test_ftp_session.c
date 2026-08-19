// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The FTP client session (services/file_transfer/ftp/ftp_session.c) over a real TCP client.
//
// RFC 959 sec 5.4 prints the minimum command sequence a STOR needs; this drives it a step at a
// time, the way the caller does. The session is non-blocking: each call answers BUSY until the
// server's reply is in, so the loop below delivers a reply through the connected pcb and calls
// again. The host pcb model captures what goes out, so the commands are read off the wire.
//
// test_ftp covers the wire codec these steps build and parse. This covers the state machine that
// orders them, its refusals, and what it does with a connection that never comes up.

#include "services/file_transfer/ftp/ftp_session/ftp_session.c"

#include "network_drivers/transport/tcp/client/client.h"

#include <string.h>

#include <unity.h>

static uint8_t g_payload[64];

static size_t src_bytes(void *ctx, size_t offset, uint8_t *out, size_t cap)
{
    (void)ctx;
    const size_t left = offset < sizeof(g_payload) ? sizeof(g_payload) - offset : 0;
    const size_t n = cap < left ? cap : left;
    mem.cpy(out, g_payload + offset, n);
    return n;
}

void setUp(void)
{
    set_millis(0);
    protocore_net_host_reply_reset();
    protocore_net_host_tx_len = 0;
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        memset(&protocore_net_host_pcbs[i], 0, sizeof(protocore_pcb));
    }
    for (int i = 0; i < PROTOCORE_CLIENT_CONNS; i++)
    {
        TcpClient.cid = i;
        TcpClient.close(protocore_tcp_client_span());
    }
    uint8_t *work = protocore_ftp_session_span();
    if (work)
    {
        FTP_SESSION_CTX(work)->step = (uint8_t)FTP_STEP_IDLE;
        FTP_SESSION_CTX(work)->ctrl = -1;
        FTP_SESSION_CTX(work)->data = -1;
    }
    for (size_t i = 0; i < sizeof(g_payload); i++)
    {
        g_payload[i] = (uint8_t)i;
    }
}

void tearDown(void)
{
    protocore_net_host_reply_reset();
}

// The control connection the session opened: the pcb the pool handed out that carries a recv
// callback. This is the seam the stack itself would deliver through.
static protocore_pcb *wired_pcb(void)
{
    for (int i = 0; i < PROTOCORE_NET_HOST_PCBS; i++)
    {
        if (protocore_net_host_pcbs[i].in_use && protocore_net_host_pcbs[i].on_recv != NULL)
        {
            return &protocore_net_host_pcbs[i];
        }
    }
    return NULL;
}

// Hand the server's reply to the control connection, the way the stack does.
static void deliver(const char *line)
{
    protocore_pcb *p = wired_pcb();
    TEST_ASSERT_NOT_NULL(p);
    protocore_pbuf b;
    memset(&b, 0, sizeof(b));
    b.payload = (void *)(uintptr_t)line;
    b.len = (uint16_t)strlen(line);
    b.tot_len = b.len;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_NET_OK, p->on_recv(p->arg, p, &b, PROTOCORE_NET_OK));
}

static FtpTarget g_target;

static protocore_ftp_state step(void)
{
    g_target.host = "10.0.0.9";
    g_target.port = 21;
    g_target.user = "anonymous";
    g_target.pass = "guest@";
    FtpSession.store_args.target = &g_target;
    FtpSession.store_args.remote_path = "/upload/O1234.nc";
    FtpSession.store_args.total = sizeof(g_payload);
    FtpSession.store_args.src = src_bytes;
    FtpSession.store_args.ctx = NULL;
    FtpSession.store(protocore_ftp_session_span());
    return FtpSession.value;
}

// The octets the session put on the wire, NUL terminated for a substring search.
static const char *sent(void)
{
    return tcp_captured();
}

// ---------------------------------------------------------------------------
// The borrow
// ---------------------------------------------------------------------------

// The span is the PROTOCORE_FTP_SESSION_BORROW bytes the session lives in.
void test_the_span_carves_the_session(void)
{
    uint8_t *work = protocore_ftp_session_span();
    TEST_ASSERT_NOT_NULL(work);
    TEST_ASSERT_EQUAL_PTR(work, (uint8_t *)FTP_SESSION_CTX(work));
    TEST_ASSERT_TRUE(sizeof(FtpSessionCtx) <= PROTOCORE_FTP_SESSION_BORROW);
}

// A borrow arrives zeroed and 0 is a valid socket handle, so the carve seats both handles closed.
void test_both_handles_are_seated_closed(void)
{
    uint8_t *work = protocore_ftp_session_span();
    TEST_ASSERT_EQUAL_INT(-1, FTP_SESSION_CTX(work)->ctrl);
    TEST_ASSERT_EQUAL_INT(-1, FTP_SESSION_CTX(work)->data);
}

// NULL is what a short pool hands over, and the session writes through the context, so it does
// nothing rather than fault.
void test_a_null_borrow_is_refused(void)
{
    FtpSession.value = PROTOCORE_FTP_READY;
    FtpSession.store(NULL);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_READY, (int)FtpSession.value);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// ---------------------------------------------------------------------------
// What it refuses before dialing
// ---------------------------------------------------------------------------

// A transfer with no host, no path or no source has nothing to send, so no connection is opened.
void test_an_incomplete_request_opens_nothing(void)
{
    uint8_t *work = protocore_ftp_session_span();

    FtpSession.store_args.target = NULL;
    FtpSession.store_args.remote_path = "/x";
    FtpSession.store_args.total = 4;
    FtpSession.store_args.src = src_bytes;
    FtpSession.store(work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_FAILED, (int)FtpSession.value);

    g_target.host = "10.0.0.9";
    g_target.port = 21;
    FtpSession.store_args.target = &g_target;
    FtpSession.store_args.remote_path = "";
    FtpSession.store(work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_FAILED, (int)FtpSession.value);

    FtpSession.store_args.remote_path = "/x";
    FtpSession.store_args.src = NULL;
    FtpSession.store(work);
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_FAILED, (int)FtpSession.value);

    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// ---------------------------------------------------------------------------
// RFC 959 sec 5.4: the command sequence
// ---------------------------------------------------------------------------

// The first call dials and waits for the greeting: nothing is sent until the server speaks first
// (RFC 959 sec 5.4 - the server-PI sends 220 on connect).
void test_the_session_waits_for_the_greeting_before_sending(void)
{
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    TEST_ASSERT_NOT_NULL(wired_pcb());
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// USER then PASS then TYPE I then EPSV, each sent only after its predecessor's reply lands. The
// order is what RFC 959 sec 5.4 prescribes for a STOR.
void test_the_login_sequence_is_user_pass_type_epsv(void)
{
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());

    deliver("220 ready\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    TEST_ASSERT_NOT_NULL(strstr(sent(), "USER anonymous\r\n"));

    deliver("331 need password\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    TEST_ASSERT_NOT_NULL(strstr(sent(), "PASS guest@\r\n"));

    deliver("230 logged in\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    TEST_ASSERT_NOT_NULL(strstr(sent(), "TYPE I\r\n"));

    deliver("200 type set\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    TEST_ASSERT_NOT_NULL(strstr(sent(), "EPSV\r\n"));
}

// A login the server refuses ends the transfer rather than sending the next command.
void test_a_refused_login_ends_the_transfer(void)
{
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    deliver("220 ready\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    deliver("530 not logged in\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_FAILED, (int)step());
    TEST_ASSERT_NULL(strstr(sent(), "TYPE I\r\n"));
}

// RFC 2428 sec 3: a 229 answers EPSV with the port in (|||port|), and the data connection reuses
// the control connection's host. RFC 959 sec 4.1.2's PASV is the fallback the next test drives.
void test_epsv_opens_the_data_connection_and_sends_stor(void)
{
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    deliver("220 ready\r\n");
    (void)step();
    deliver("331 need password\r\n");
    (void)step();
    deliver("230 logged in\r\n");
    (void)step();
    deliver("200 type set\r\n");
    (void)step();

    deliver("229 Entering Extended Passive Mode (|||41234|)\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    TEST_ASSERT_NOT_NULL(strstr(sent(), "STOR /upload/O1234.nc\r\n"));
}

// A server that answers 500 to EPSV gets PASV instead, which is why the fallback is there.
void test_a_refused_epsv_falls_back_to_pasv(void)
{
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    deliver("220 ready\r\n");
    (void)step();
    deliver("331 need password\r\n");
    (void)step();
    deliver("230 logged in\r\n");
    (void)step();
    deliver("200 type set\r\n");
    (void)step();

    deliver("500 unknown command\r\n");
    TEST_ASSERT_EQUAL_INT((int)PROTOCORE_FTP_BUSY, (int)step());
    TEST_ASSERT_NOT_NULL(strstr(sent(), "PASV\r\n"));
}
