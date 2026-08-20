// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/net/smtp/smtp.h"
#include <stdio.h>
#include <string.h>

#include <unity.h>

#define REPLY_MAX 12
#define SENT_MAX 8192

typedef struct
{
    const char *replies[REPLY_MAX];
    size_t reply_n;
    size_t idx;
    char sent[SENT_MAX];
    size_t sent_len;
    proto_bool dribble;
    size_t dribble_pos;
    const char *fail_send_prefix;
    int upgrades;
    proto_bool upgrade_ok;
} Mock;

static void mock_init(Mock *m)
{
    memset(m, 0, sizeof *m);
    m->upgrade_ok = PROTO_TRUE;
}

static void mock_replies(Mock *m, const char *const *r, size_t n)
{
    TEST_ASSERT_TRUE(n <= REPLY_MAX);
    for (size_t i = 0; i < n; i++)
    {
        m->replies[i] = r[i];
    }
    m->reply_n = n;
}

static proto_bool mock_starttls(void *c)
{
    Mock *m = (Mock *)c;
    m->upgrades++;
    return m->upgrade_ok;
}

static int mock_send(void *c, const uint8_t *d, size_t n)
{
    Mock *m = (Mock *)c;
    if (m->fail_send_prefix)
    {
        size_t pn = strlen(m->fail_send_prefix);
        if (n >= pn && memcmp(d, m->fail_send_prefix, pn) == 0)
        {
            return (int)n - 1;
        }
    }
    TEST_ASSERT_TRUE(m->sent_len + n < SENT_MAX);
    memcpy(m->sent + m->sent_len, d, n);
    m->sent_len += n;
    m->sent[m->sent_len] = '\0';
    return (int)n;
}

static int mock_recv(void *c, uint8_t *b, size_t cap)
{
    Mock *m = (Mock *)c;
    if (m->idx >= m->reply_n)
    {
        return -1;
    }
    const char *r = m->replies[m->idx];
    size_t rlen = strlen(r);
    if (m->dribble)
    {
        if (m->dribble_pos >= rlen)
        {
            m->idx++;
            m->dribble_pos = 0;
            if (m->idx >= m->reply_n)
            {
                return -1;
            }
        }
        b[0] = (uint8_t)m->replies[m->idx][m->dribble_pos++];
        if (m->dribble_pos >= strlen(m->replies[m->idx]))
        {
            m->idx++;
            m->dribble_pos = 0;
        }
        return 1;
    }
    size_t n = rlen < cap ? rlen : cap;
    memcpy(b, r, n);
    m->idx++;
    return (int)n;
}

static const char *const HAPPY[] = {"220 mail.example.net ESMTP\r\n",
                                    "250-mail.example.net\r\n250 OK\r\n",
                                    "250 2.1.0 Ok\r\n",
                                    "250 2.1.5 Ok\r\n",
                                    "354 End data with <CR><LF>.<CR><LF>\r\n",
                                    "250 2.0.0 Ok: queued\r\n",
                                    "221 2.0.0 Bye\r\n"};
#define HAPPY_N (sizeof HAPPY / sizeof HAPPY[0])

static void mock_happy(Mock *m)
{
    mock_init(m);
    mock_replies(m, HAPPY, HAPPY_N);
}

// The two records this suite was written against, and the one call that seats them. SmtpNs groups
// its arguments by concern instead, so the fields land on session, auth, envelope and content.
typedef struct
{
    const char *host;
    uint16_t port;
    SmtpSecurity security;
    const char *user;
    const char *pass;
    const char *from;
    const char *helo;
} SmtpConfig;

typedef struct
{
    const char *to;
    const char *subject;
    const char *body;
} SmtpMessage;

static void seat(const SmtpConfig *c, const SmtpMessage *m, SmtpSendFn send, SmtpRecvFn recv, SmtpStartTlsFn starttls,
                 void *ctx)
{
    SmtpV.session.host = c->host;
    SmtpV.session.port = c->port;
    SmtpV.session.security = c->security;
    SmtpV.session.client_name = c->helo;
    SmtpV.auth.user = c->user;
    SmtpV.auth.pass = c->pass;
    SmtpV.envelope.reverse_path = c->from;
    SmtpV.envelope.forward_path = m->to;
    SmtpV.content.subject = m->subject;
    SmtpV.content.body = m->body;
    SmtpV.transport.send = send;
    SmtpV.transport.recv = recv;
    SmtpV.transport.starttls = starttls;
    SmtpV.transport.ctx = ctx;
}

// Walk the session over the seam the caller supplies.
static SmtpResult smtp_run(const SmtpConfig *c, const SmtpMessage *m, SmtpSendFn send, SmtpRecvFn recv,
                           SmtpStartTlsFn starttls, void *ctx)
{
    if (!c || !m)
    {
        return SMTP_ERR_ARG; // the flat call validated its two records before reading either
    }
    seat(c, m, send, recv, starttls, ctx);
    Smtp.run(protocore_smtp_span());
    return SmtpV.result;
}

// The entry that opens its own transport instead of taking one.
static SmtpResult smtp_send(const SmtpConfig *c, const SmtpMessage *m)
{
    if (!c || !m)
    {
        return SMTP_ERR_ARG;
    }
    seat(c, m, NULL, NULL, NULL, NULL);
    SmtpV.send(protocore_smtp_span());
    return SmtpV.result;
}

static SmtpConfig base_cfg(void)
{
    SmtpConfig c;
    c.host = "mail.example.net";
    c.port = 25;
    c.security = SMTP_PLAIN;
    c.user = NULL;
    c.pass = NULL;
    c.from = "device@example.net";
    c.helo = "esp32";
    return c;
}
static SmtpMessage base_msg(void)
{
    SmtpMessage m;
    m.to = "ops@example.net";
    m.subject = "Alert";
    m.body = "sensor tripped";
    return m;
}

#define SENT(m, s) (strstr((m).sent, (s)) != NULL)

void setUp(void)
{
}
void tearDown(void)
{
}

void test_happy_path_no_auth(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));

    TEST_ASSERT_TRUE(SENT(m, "EHLO esp32\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "MAIL FROM:<device@example.net>\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "RCPT TO:<ops@example.net>\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "DATA\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "QUIT\r\n"));

    TEST_ASSERT_TRUE(SENT(m, "Subject: Alert\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "To: <ops@example.net>\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "sensor tripped\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "\r\n.\r\n"));

    TEST_ASSERT_FALSE(SENT(m, "AUTH"));
}

void test_auth_login(void)
{
    static const char *const r[] = {"220 ESMTP\r\n",    "250 OK\r\n", "334 VXNlcm5hbWU6\r\n", "334 UGFzc3dvcmQ6\r\n",
                                    "235 2.7.0 Ok\r\n", "250 Ok\r\n", "250 Ok\r\n",           "354 go\r\n",
                                    "250 queued\r\n",   "221 Bye\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    c.user = "user";
    c.pass = "pass";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "AUTH LOGIN\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "dXNlcg==\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "cGFzcw==\r\n"));
}

void test_auth_rejected(void)
{
    static const char *const r[] = {"220 ESMTP\r\n", "250 OK\r\n", "334 x\r\n", "334 y\r\n",
                                    "535 5.7.8 auth failed\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    c.user = "user";
    c.pass = "wrong";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_AUTH, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_greeting_not_ready(void)
{
    static const char *const r[] = {"554 no service\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, 1);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_rcpt_rejected(void)
{
    static const char *const r[] = {"220 ESMTP\r\n", "250 OK\r\n", "250 Ok\r\n", "550 5.1.1 no such user\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_data_refused(void)
{
    static const char *const r[] = {"220 ESMTP\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n", "451 try later\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_dot_stuffing(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    msg.body = "line1\n.hidden\n..two dots\nlast";
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "..hidden\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "...two dots\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "last\r\n.\r\n"));
}

void test_multiline_reply_and_lf_body(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    msg.body = "a\nb";
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "a\r\nb\r\n"));
}

void test_partial_reads_dribble(void)
{
    Mock m;
    mock_happy(&m);
    m.dribble = PROTO_TRUE;
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_missing_required_arg(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    c.from = "";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_io_error_when_server_hangs(void)
{
    Mock m;
    mock_init(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

static SmtpResult dialogue(const char *const *replies, size_t n, SmtpConfig c, SmtpMessage msg)
{
    static Mock m;
    mock_init(&m);
    mock_replies(&m, replies, n);
    return smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m);
}

#define DIALOGUE(cfg, msg, ...)                                                                                        \
    dialogue((const char *[]){__VA_ARGS__}, sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *), (cfg), (msg))

void test_reply_buffer_overflow(void)
{
    static char huge[1024];
    huge[0] = '\0';
    size_t n = 0;
    while (n < 600)
    {
        const char *line = "250-continuation\r\n";
        size_t l = strlen(line);
        memcpy(huge + n, line, l);
        n += l;
    }
    huge[n] = '\0';
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(base_cfg(), base_msg(), huge));
}

void test_command_send_fails(void)
{
    static const char *const r[] = {"220 ESMTP\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, 1);
    m.fail_send_prefix = "EHLO";
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_body_send_fails(void)
{
    static const char *const r[] = {"220 ESMTP\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n", "354 go\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    m.fail_send_prefix = "From:";
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_auth_secret_too_long(void)
{
    SmtpConfig c = base_cfg();
    static char longuser[401];
    memset(longuser, 'u', sizeof longuser - 1);
    longuser[sizeof longuser - 1] = '\0';
    c.user = longuser;
    c.pass = "pw";
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(c, base_msg(), "220 ESMTP\r\n", "250 OK\r\n", "334 x\r\n"));
}

void test_io_error_at_each_step(void)
{
    SmtpConfig c = base_cfg();
    SmtpConfig cu = base_cfg();
    cu.user = "user";
    cu.pass = "pass";
    SmtpMessage msg = base_msg();

    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n", "334 a\r\n", "334 b\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO,
                          DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n", "354 go\r\n"));
}

void test_protocol_error_at_each_step(void)
{
    SmtpConfig c = base_cfg();
    SmtpConfig cu = base_cfg();
    cu.user = "user";
    cu.pass = "pass";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL, DIALOGUE(c, msg, "220 x\r\n", "500 no ehlo\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_AUTH, DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n", "500 no auth\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_AUTH, DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n", "334 a\r\n", "500 bad user\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "550 denied\r\n"));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n",
                                                      "354 go\r\n", "451 rejected\r\n"));
}

void test_command_line_overflows(void)
{
    static char big[301];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    SmtpConfig ch = base_cfg();
    ch.helo = big;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(ch, base_msg(), "220 x\r\n"));

    SmtpConfig cf = base_cfg();
    cf.from = big;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(cf, base_msg(), "220 x\r\n", "250 OK\r\n"));

    SmtpMessage mt = base_msg();
    mt.to = big;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(base_cfg(), mt, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n"));
}

void test_message_header_overflow(void)
{
    static char bigsub[2101];
    memset(bigsub, 'S', sizeof bigsub - 1);
    bigsub[sizeof bigsub - 1] = '\0';
    SmtpMessage msg = base_msg();
    msg.subject = bigsub;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(base_cfg(), msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n",
                                                      "250 Ok\r\n", "354 go\r\n"));
}

void test_cr_in_body_dropped(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    msg.body = "x\r\ny";
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "x\r\ny\r\n"));
}

void test_build_message_boundary_overflows(void)
{
    static const char *const to_data[] = {"220 x\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n", "354 go\r\n"};
    static const char *const suffixes[] = {"", "\n", "\n."};
    static char body[2070];
    proto_bool saw_overflow = PROTO_FALSE;
    for (size_t L = 1850; L <= 2060; L++)
    {
        for (unsigned s = 0; s < sizeof suffixes / sizeof suffixes[0]; s++)
        {
            size_t sl = strlen(suffixes[s]);
            TEST_ASSERT_TRUE(L + sl < sizeof body);
            memset(body, 'x', L);
            memcpy(body + L, suffixes[s], sl);
            body[L + sl] = '\0';
            SmtpMessage msg = base_msg();
            msg.body = body;
            SmtpResult r = dialogue(to_data, sizeof to_data / sizeof to_data[0], base_cfg(), msg);
            TEST_ASSERT_NOT_EQUAL(SMTP_OK, r);
            if (r == SMTP_ERR_OVERFLOW)
            {
                saw_overflow = PROTO_TRUE;
            }
        }
    }
    TEST_ASSERT_TRUE(saw_overflow);
}

void test_host_smtp_send_stub(void)
{
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_CONNECT, smtp_send(&c, &msg));
}

static const char *const STARTTLS_SCRIPT[] = {"220 mail.example.net ESMTP\r\n",
                                              "250-mail.example.net\r\n250-STARTTLS\r\n250 OK\r\n",
                                              "220 2.0.0 Ready to start TLS\r\n",
                                              "250-mail.example.net\r\n250 OK\r\n",
                                              "250 2.1.0 Ok\r\n",
                                              "250 2.1.5 Ok\r\n",
                                              "354 End data with <CR><LF>.<CR><LF>\r\n",
                                              "250 2.0.0 Ok: queued\r\n",
                                              "221 2.0.0 Bye\r\n"};

static void starttls_mock(Mock *m)
{
    mock_init(m);
    mock_replies(m, STARTTLS_SCRIPT, sizeof STARTTLS_SCRIPT / sizeof STARTTLS_SCRIPT[0]);
}

void test_starttls_upgrades_and_reissues_ehlo(void)
{
    Mock m;
    starttls_mock(&m);
    SmtpConfig c = base_cfg();
    c.port = 587;
    c.security = SMTP_STARTTLS;
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
    TEST_ASSERT_EQUAL_INT(1, m.upgrades);
    TEST_ASSERT_TRUE(SENT(m, "STARTTLS\r\n"));

    const char *first = strstr(m.sent, "EHLO");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(strstr(first + 1, "EHLO"));

    TEST_ASSERT_TRUE(strstr(m.sent, "STARTTLS\r\n") < strstr(m.sent, "MAIL FROM"));
}

void test_starttls_not_advertised_fails_before_auth(void)
{

    static const char *const r[] = {"220 mail.example.net ESMTP\r\n", "250-mail.example.net\r\n250 OK\r\n",
                                    "221 2.0.0 Bye\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    c.port = 587;
    c.security = SMTP_STARTTLS;
    c.user = "device";
    c.pass = "hunter2";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_NO_STARTTLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
    TEST_ASSERT_EQUAL_INT(0, m.upgrades);
    TEST_ASSERT_FALSE(SENT(m, "AUTH"));
    TEST_ASSERT_FALSE(SENT(m, "aHVudGVyMg=="));
    TEST_ASSERT_FALSE(SENT(m, "MAIL FROM"));
}

void test_starttls_partial_keyword_is_not_a_match(void)
{

    static const char *const r[] = {"220 mail.example.net ESMTP\r\n",
                                    "250-mail.example.net\r\n250-STARTTLSX\r\n250 OK\r\n", "221 2.0.0 Bye\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    c.port = 587;
    c.security = SMTP_STARTTLS;
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_NO_STARTTLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
}

void test_starttls_capability_match_is_case_insensitive(void)
{
    Mock m;
    starttls_mock(&m);
    m.replies[1] = "250-mail.example.net\r\n250-StartTls\r\n250 OK\r\n";
    SmtpConfig c = base_cfg();
    c.port = 587;
    c.security = SMTP_STARTTLS;
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
    TEST_ASSERT_EQUAL_INT(1, m.upgrades);
}

void test_starttls_server_refuses_the_upgrade(void)
{
    Mock m;
    starttls_mock(&m);
    m.replies[2] = "454 4.7.0 TLS not available right now\r\n";
    SmtpConfig c = base_cfg();
    c.port = 587;
    c.security = SMTP_STARTTLS;
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_TLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
    TEST_ASSERT_EQUAL_INT(0, m.upgrades);
}

void test_starttls_handshake_failure_aborts(void)
{
    Mock m;
    starttls_mock(&m);
    m.upgrade_ok = PROTO_FALSE;
    SmtpConfig c = base_cfg();
    c.port = 587;
    c.security = SMTP_STARTTLS;
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_TLS, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
    TEST_ASSERT_EQUAL_INT(1, m.upgrades);
    TEST_ASSERT_FALSE(SENT(m, "MAIL FROM"));
}

void test_starttls_without_an_upgrade_callback_is_an_arg_error(void)
{
    Mock m;
    starttls_mock(&m);
    SmtpConfig c = base_cfg();
    c.port = 587;
    c.security = SMTP_STARTTLS;
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_plain_ignores_an_advertised_starttls(void)
{

    static const char *const r[] = {"220 mail.example.net ESMTP\r\n",
                                    "250-mail.example.net\r\n250-STARTTLS\r\n250 OK\r\n",
                                    "250 2.1.0 Ok\r\n",
                                    "250 2.1.5 Ok\r\n",
                                    "354 End data with <CR><LF>.<CR><LF>\r\n",
                                    "250 2.0.0 Ok: queued\r\n",
                                    "221 2.0.0 Bye\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
    TEST_ASSERT_EQUAL_INT(0, m.upgrades);
    TEST_ASSERT_FALSE(SENT(m, "STARTTLS\r\n"));
}

void test_reply_parser_skips_malformed_lines(void)
{
    static const char *const junk[] = {
        "ab\r\n", "+ab\r\n", "Zab\r\n", "2 b\r\n", "2Zb\r\n", "22 \r\n", "22Z\r\n", "22\rx\r\n",
    };
    for (unsigned i = 0; i < sizeof junk / sizeof junk[0]; i++)
    {
        static char greeting[64];
        snprintf(greeting, sizeof greeting, "%s220 mail.example.net ESMTP\r\n", junk[i]);
        Mock m;
        mock_happy(&m);
        m.replies[0] = greeting;
        SmtpConfig c = base_cfg();
        SmtpMessage msg = base_msg();
        TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    }
}

void test_reply_bare_three_digit_line_is_final(void)
{
    Mock m;
    mock_happy(&m);
    m.replies[0] = "220\r\n";
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_ehlo_capability_scan_edges(void)
{
    static const char *const ehlo_replies[] = {
        "250-\r\n250-STARTTLS\r\n250 OK\r\n",
        "250-a\rb\r\n250-STARTTLS\r\n250 OK\r\n",
        "250-STARTTLS FOO\r\n250 OK\r\n",
        "250-8BITMIME\r\n250-STARTTLS\r\n250 OK\r\n",
    };
    for (unsigned i = 0; i < sizeof ehlo_replies / sizeof ehlo_replies[0]; i++)
    {
        Mock m;
        starttls_mock(&m);
        m.replies[1] = ehlo_replies[i];
        SmtpConfig c = base_cfg();
        c.port = 587;
        c.security = SMTP_STARTTLS;
        SmtpMessage msg = base_msg();
        TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
        TEST_ASSERT_EQUAL_INT(1, m.upgrades);
    }
}

void test_null_optional_fields(void)
{
    for (int variant = 0; variant < 2; variant++)
    {
        Mock m;
        mock_happy(&m);
        SmtpConfig c = base_cfg();
        c.helo = variant ? "" : NULL;
        SmtpMessage msg = base_msg();
        msg.subject = NULL;
        msg.body = NULL;
        TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
        // A caller that names no Domain gets SMTP_DEFAULT_CLIENT_NAME (RFC 5321 sec 4.1.1.1).
        TEST_ASSERT_TRUE(SENT(m, "EHLO protocore\r\n"));
        TEST_ASSERT_TRUE(SENT(m, "Subject: \r\n"));
        TEST_ASSERT_TRUE(SENT(m, "\r\n\r\n.\r\n"));
    }
}

void test_null_password_sends_empty_secret(void)
{
    static const char *const r[] = {"220 ESMTP\r\n",  "250 OK\r\n", "334 VXNlcm5hbWU6\r\n", "334 UGFzc3dvcmQ6\r\n",
                                    "235 Ok\r\n",     "250 Ok\r\n", "250 Ok\r\n",           "354 go\r\n",
                                    "250 queued\r\n", "221 Bye\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    SmtpConfig c = base_cfg();
    c.user = "user";
    c.pass = NULL;
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "dXNlcg==\r\n"));
    TEST_ASSERT_FALSE(SENT(m, "AUTH LOGIN\r\n\r\n"));
}

void test_empty_user_skips_auth(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    c.user = "";
    c.pass = "pw";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_FALSE(SENT(m, "AUTH"));
}

void test_arg_validation_rejects_each_missing_field(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();

    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(NULL, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, NULL, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, &msg, NULL, mock_recv, NULL, &m));
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, &msg, mock_send, NULL, NULL, &m));

    SmtpConfig nohost = base_cfg();
    nohost.host = NULL;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&nohost, &msg, mock_send, mock_recv, NULL, &m));

    SmtpConfig nofrom = base_cfg();
    nofrom.from = NULL;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&nofrom, &msg, mock_send, mock_recv, NULL, &m));

    SmtpMessage noto = base_msg();
    noto.to = NULL;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, &noto, mock_send, mock_recv, NULL, &m));

    SmtpMessage emptyto = base_msg();
    emptyto.to = "";
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, &emptyto, mock_send, mock_recv, NULL, &m));

    TEST_ASSERT_EQUAL_size_t(0, m.sent_len);
}

void test_rcpt_251_is_accepted(void)
{
    Mock m;
    mock_happy(&m);
    m.replies[3] = "251 User not local; will forward\r\n";
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_command_helper_send_failure(void)
{
    static const char *const r[] = {"220 ESMTP\r\n", "250 OK\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    m.fail_send_prefix = "MAIL FROM";
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}
