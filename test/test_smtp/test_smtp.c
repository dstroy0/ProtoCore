// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SMTP client dialogue engine (services/net/smtp/smtp_run). A scripted
// mock server returns one reply per recv (non-pipelined SMTP) and captures the client's
// commands, so the full RFC 5321 exchange - greeting, EHLO, AUTH LOGIN, MAIL/RCPT/DATA,
// dot-stuffing, the terminating "." - is verified without any network or TLS.

#include "services/net/smtp/smtp.h"
#include <stdio.h>

#include <unity.h>

// The scripted server. Replies are pointers into string literals or into a test's own buffer;
// the capture is one fixed region, sized well past the longest dialogue any test drives.
#define REPLY_MAX 12
#define SENT_MAX 8192

typedef struct
{
    const char *replies[REPLY_MAX]; // server -> client, one per recv turn
    size_t reply_n;
    size_t idx;
    char sent[SENT_MAX]; // everything the client wrote, NUL-terminated for strstr
    size_t sent_len;
    proto_bool dribble; // return replies one byte at a time (exercise the accumulate loop)
    size_t dribble_pos;
    const char *fail_send_prefix; // a write beginning with this returns short (I/O failure)
    int upgrades;                 // how many times the engine asked to go TLS
    proto_bool upgrade_ok;        // make the simulated handshake fail
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

// Stand-in for the real TLS upgrade: records the call so a test can assert it happened at the
// right point in the dialogue, and can simulate a failed handshake.
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
            return (int)n - 1; // short write -> send_str() / the body send sees != n
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
        return -1; // no more scripted data -> I/O error
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

// A standard successful conversation, with the message-acceptance reply configurable.
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

// Was the string written by the client at some point in the dialogue?
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
    // Commands, in order.
    TEST_ASSERT_TRUE(SENT(m, "EHLO esp32\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "MAIL FROM:<device@example.net>\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "RCPT TO:<ops@example.net>\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "DATA\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "QUIT\r\n"));
    // Message headers + body + terminator.
    TEST_ASSERT_TRUE(SENT(m, "Subject: Alert\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "To: <ops@example.net>\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "sensor tripped\r\n"));
    TEST_ASSERT_TRUE(SENT(m, "\r\n.\r\n")); // end-of-DATA
    // No AUTH when no user configured.
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
    TEST_ASSERT_TRUE(SENT(m, "dXNlcg==\r\n")); // base64("user")
    TEST_ASSERT_TRUE(SENT(m, "cGFzcw==\r\n")); // base64("pass")
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
    msg.body = "line1\n.hidden\n..two dots\nlast"; // lines starting with '.' must be stuffed
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "..hidden\r\n"));    // '.' -> '..'
    TEST_ASSERT_TRUE(SENT(m, "...two dots\r\n")); // '..' -> '...'
    TEST_ASSERT_TRUE(SENT(m, "last\r\n.\r\n"));   // real terminator intact
}

void test_multiline_reply_and_lf_body(void)
{
    Mock m;
    mock_happy(&m); // EHLO reply is multi-line ("250-...\r\n250 OK\r\n")
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    msg.body = "a\nb"; // bare LF must be normalized to CRLF
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "a\r\nb\r\n"));
}

void test_partial_reads_dribble(void)
{
    Mock m;
    mock_happy(&m);
    m.dribble = PROTO_TRUE; // deliver every reply one byte at a time
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_missing_required_arg(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    c.from = ""; // empty sender
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_ARG, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

void test_io_error_when_server_hangs(void)
{
    Mock m; // no replies scripted -> recv returns -1 on the greeting read
    mock_init(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

// Run a dialogue with the given scripted replies and return smtp_run's result.
static SmtpResult dialogue(const char *const *replies, size_t n, SmtpConfig c, SmtpMessage msg)
{
    static Mock m; // 8 KB of capture, kept off the stack of a 600-iteration sweep
    mock_init(&m);
    mock_replies(&m, replies, n);
    return smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m);
}

// The scripted replies read best written inline, so the count comes from the literal itself.
#define DIALOGUE(cfg, msg, ...)                                                                                        \
    dialogue((const char *[]){__VA_ARGS__}, sizeof((const char *[]){__VA_ARGS__}) / sizeof(const char *), (cfg), (msg))

// An overlong reply that never completes (all continuation lines) overflows the reply
// buffer; smtp_run maps that to an I/O error on the greeting read.
void test_reply_buffer_overflow(void)
{
    static char huge[1024];
    huge[0] = '\0';
    size_t n = 0;
    while (n < 600)
    {
        const char *line = "250-continuation\r\n"; // > PC_SMTP_REPLY_MAX, no final line
        size_t l = strlen(line);
        memcpy(huge + n, line, l);
        n += l;
    }
    huge[n] = '\0';
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(base_cfg(), base_msg(), huge));
}

// A short write on a command line (here EHLO) is an I/O error.
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

// The DATA payload send failing (short write) is an I/O error.
void test_body_send_fails(void)
{
    static const char *const r[] = {"220 ESMTP\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n", "354 go\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    m.fail_send_prefix = "From:"; // only the DATA payload begins "From: <...>"
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

// An AUTH secret too long to base64-encode into the line buffer overflows.
void test_auth_secret_too_long(void)
{
    SmtpConfig c = base_cfg();
    static char longuser[401]; // base64 grows it past PC_SMTP_LINE_MAX
    memset(longuser, 'u', sizeof longuser - 1);
    longuser[sizeof longuser - 1] = '\0';
    c.user = longuser;
    c.pass = "pw";
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(c, base_msg(), "220 ESMTP\r\n", "250 OK\r\n", "334 x\r\n"));
}

// I/O failure (server hangs up) at each step of the dialogue -> SmtpResult::SMTP_ERR_IO.
void test_io_error_at_each_step(void)
{
    SmtpConfig c = base_cfg();
    SmtpConfig cu = base_cfg();
    cu.user = "user";
    cu.pass = "pass";
    SmtpMessage msg = base_msg();
    // greeting ok, then hang before: EHLO / MAIL(no auth) / AUTH(user) / pass-leg / RCPT / DATA / final.
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n"));                // EHLO
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n"));  // MAIL FROM
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n")); // AUTH LOGIN
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO,
                          DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n", "334 a\r\n", "334 b\r\n")); // pass leg
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n")); // RCPT
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n")); // DATA
    TEST_ASSERT_EQUAL_INT( // final acceptance read
        SMTP_ERR_IO, DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n", "354 go\r\n"));
}

// A wrong reply code at each step -> the step-specific SmtpResult.
void test_protocol_error_at_each_step(void)
{
    SmtpConfig c = base_cfg();
    SmtpConfig cu = base_cfg();
    cu.user = "user";
    cu.pass = "pass";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL, DIALOGUE(c, msg, "220 x\r\n", "500 no ehlo\r\n")); // EHLO != 250
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_AUTH,
                          DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n", "500 no auth\r\n")); // AUTH != 334
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_AUTH,
                          DIALOGUE(cu, msg, "220 x\r\n", "250 OK\r\n", "334 a\r\n", "500 bad user\r\n")); // user != 334
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_PROTOCOL,
                          DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "550 denied\r\n")); // MAIL != 250
    TEST_ASSERT_EQUAL_INT(                                                                // final acceptance != 250
        SMTP_ERR_PROTOCOL,
        DIALOGUE(c, msg, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n", "250 Ok\r\n", "354 go\r\n", "451 rejected\r\n"));
}

// Each outgoing command line that is built with snprintf overflows when its variable
// field (helo / from / to) is longer than PC_SMTP_LINE_MAX.
void test_command_line_overflows(void)
{
    static char big[301];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    SmtpConfig ch = base_cfg();
    ch.helo = big;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(ch, base_msg(), "220 x\r\n")); // EHLO line

    SmtpConfig cf = base_cfg();
    cf.from = big;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW, DIALOGUE(cf, base_msg(), "220 x\r\n", "250 OK\r\n")); // MAIL FROM line

    SmtpMessage mt = base_msg();
    mt.to = big;
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_OVERFLOW,
                          DIALOGUE(base_cfg(), mt, "220 x\r\n", "250 OK\r\n", "250 Ok\r\n")); // RCPT TO line
}

// A header field so long that the message headers do not fit -> build_message overflow.
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

// A CR in the body is dropped (CRLF normalization).
void test_cr_in_body_dropped(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    msg.body = "x\r\ny"; // the bare CR is stripped, the LF becomes CRLF
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_TRUE(SENT(m, "x\r\ny\r\n"));
}

// Sweep the body length across the DATA-buffer boundary so every build_message overflow
// guard (regular char, LF->CRLF, dot-stuff, trailing CRLF, terminating dot) fires at its
// own boundary length, without hard-coding exact byte counts.
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
            TEST_ASSERT_NOT_EQUAL(SMTP_OK, r); // no final 250 scripted -> never succeeds
            if (r == SMTP_ERR_OVERFLOW)
            {
                saw_overflow = PROTO_TRUE;
            }
        }
    }
    TEST_ASSERT_TRUE(saw_overflow); // the sweep crossed the buffer boundary
}

// The host build's smtp_send() is a stub (no lwIP) that reports a connect failure.
void test_host_smtp_send_stub(void)
{
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_CONNECT, smtp_send(&c, &msg));
}

// --- STARTTLS (RFC 3207) --------------------------------------------------

// A server that offers STARTTLS: greeting, EHLO capabilities, 220 to STARTTLS, then the second
// EHLO after the upgrade, then the ordinary send.
static const char *const STARTTLS_SCRIPT[] = {
    "220 mail.example.net ESMTP\r\n",
    "250-mail.example.net\r\n250-STARTTLS\r\n250 OK\r\n", // first EHLO, in the clear
    "220 2.0.0 Ready to start TLS\r\n",                   // STARTTLS accepted
    "250-mail.example.net\r\n250 OK\r\n",                 // second EHLO, encrypted
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
    // RFC 3207 sec 4.2: EHLO must be reissued after the upgrade, so it appears twice.
    const char *first = strstr(m.sent, "EHLO");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(strstr(first + 1, "EHLO"));
    // and the upgrade must precede the message data
    TEST_ASSERT_TRUE(strstr(m.sent, "STARTTLS\r\n") < strstr(m.sent, "MAIL FROM"));
}

void test_starttls_not_advertised_fails_before_auth(void)
{
    // The security property: a server (or an attacker stripping the capability) that does not offer
    // STARTTLS must not get the credentials in the clear.
    static const char *const r[] = {"220 mail.example.net ESMTP\r\n",
                                    "250-mail.example.net\r\n250 OK\r\n", // no STARTTLS advertised
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
    TEST_ASSERT_FALSE(SENT(m, "AUTH"));         // no credentials offered
    TEST_ASSERT_FALSE(SENT(m, "aHVudGVyMg==")); // base64("hunter2")
    TEST_ASSERT_FALSE(SENT(m, "MAIL FROM"));    // and no message body
}

void test_starttls_partial_keyword_is_not_a_match(void)
{
    // "STARTTLSX" is a different keyword; treating it as STARTTLS would be a silent downgrade.
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
    TEST_ASSERT_EQUAL_INT(0, m.upgrades); // never attempted, because the server said no
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
    TEST_ASSERT_FALSE(SENT(m, "MAIL FROM")); // nothing sent after a failed upgrade
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
    // Configured plaintext: the advertisement is informational, the engine must not upgrade.
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
    SmtpConfig c = base_cfg(); // SMTP_PLAIN
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, mock_starttls, &m));
    TEST_ASSERT_EQUAL_INT(0, m.upgrades);
    TEST_ASSERT_FALSE(SENT(m, "STARTTLS\r\n"));
}

// --- reply parsing edges --------------------------------------------------

// A line that is not a well-formed "NNN" status line is skipped, whatever makes it
// malformed - too short, a non-digit in any of the three code positions, or a bare CR
// that is not a line ending. The real final line after it still drives the dialogue.
void test_reply_parser_skips_malformed_lines(void)
{
    static const char *const junk[] = {
        "ab\r\n",    // shorter than a 3-digit code
        "+ab\r\n",   // first code character below '0'
        "Zab\r\n",   // first code character above '9'
        "2 b\r\n",   // second below '0'
        "2Zb\r\n",   // second above '9'
        "22 \r\n",   // third below '0'
        "22Z\r\n",   // third above '9'
        "22\rx\r\n", // a bare CR that is not a line ending
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

// A reply line that is exactly the 3-digit code with nothing after it is a final line
// (RFC 5321 sec 4.2: the space is only required when text follows).
void test_reply_bare_three_digit_line_is_final(void)
{
    Mock m;
    mock_happy(&m);
    m.replies[0] = "220\r\n";
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

// The EHLO capability scan handles the shapes a real server emits around the keyword:
// a bare "NNN-" line with no keyword, a bare CR inside the reply, a keyword carrying
// parameters, and a keyword starting with a non-letter.
void test_ehlo_capability_scan_edges(void)
{
    static const char *const ehlo_replies[] = {
        "250-\r\n250-STARTTLS\r\n250 OK\r\n",         // a line too short to hold a keyword
        "250-a\rb\r\n250-STARTTLS\r\n250 OK\r\n",     // a bare CR mid-reply
        "250-STARTTLS FOO\r\n250 OK\r\n",             // keyword followed by parameters
        "250-8BITMIME\r\n250-STARTTLS\r\n250 OK\r\n", // keyword starting with a digit
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
        TEST_ASSERT_EQUAL_INT(1, m.upgrades); // STARTTLS was still recognised in each shape
    }
}

// --- optional / missing configuration -------------------------------------

// Every optional field may be null: a null subject and body produce an empty subject
// header and an empty message, and a null or empty helo falls back to "esp32".
void test_null_optional_fields(void)
{
    for (int variant = 0; variant < 2; variant++)
    {
        Mock m;
        mock_happy(&m);
        SmtpConfig c = base_cfg();
        c.helo = variant ? "" : NULL; // both fall back to the default
        SmtpMessage msg = base_msg();
        msg.subject = NULL;
        msg.body = NULL;
        TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
        TEST_ASSERT_TRUE(SENT(m, "EHLO esp32\r\n"));
        TEST_ASSERT_TRUE(SENT(m, "Subject: \r\n")); // empty, not "(null)"
        TEST_ASSERT_TRUE(SENT(m, "\r\n\r\n.\r\n")); // empty body, then the terminator
    }
}

// A null password is sent as an empty secret rather than dereferenced.
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
    TEST_ASSERT_TRUE(SENT(m, "dXNlcg==\r\n")); // base64("user")
    TEST_ASSERT_FALSE(SENT(m, "AUTH LOGIN\r\n\r\n"));
}

// An empty username means "no credentials configured", exactly as a null one does: the
// AUTH exchange is skipped entirely.
void test_empty_user_skips_auth(void)
{
    Mock m;
    mock_happy(&m);
    SmtpConfig c = base_cfg();
    c.user = ""; // configured but empty
    c.pass = "pw";
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
    TEST_ASSERT_FALSE(SENT(m, "AUTH"));
}

// Every required argument is checked before a single byte goes out.
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

    TEST_ASSERT_EQUAL_size_t(0, m.sent_len); // nothing was ever transmitted
}

// --- envelope / transport edges -------------------------------------------

// RCPT TO may be answered 251 (user not local, will forward), which is success.
void test_rcpt_251_is_accepted(void)
{
    Mock m;
    mock_happy(&m);
    m.replies[3] = "251 User not local; will forward\r\n";
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_OK, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

// A short write on a command issued through the command() helper (rather than the EHLO
// that greet_ehlo writes directly) is reported as an I/O error.
void test_command_helper_send_failure(void)
{
    static const char *const r[] = {"220 ESMTP\r\n", "250 OK\r\n"};
    Mock m;
    mock_init(&m);
    mock_replies(&m, r, sizeof r / sizeof r[0]);
    m.fail_send_prefix = "MAIL FROM"; // goes out via cmd_expect -> command -> send_str
    SmtpConfig c = base_cfg();
    SmtpMessage msg = base_msg();
    TEST_ASSERT_EQUAL_INT(SMTP_ERR_IO, smtp_run(&c, &msg, mock_send, mock_recv, NULL, &m));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reply_parser_skips_malformed_lines);
    RUN_TEST(test_reply_bare_three_digit_line_is_final);
    RUN_TEST(test_ehlo_capability_scan_edges);
    RUN_TEST(test_null_optional_fields);
    RUN_TEST(test_null_password_sends_empty_secret);
    RUN_TEST(test_empty_user_skips_auth);
    RUN_TEST(test_arg_validation_rejects_each_missing_field);
    RUN_TEST(test_rcpt_251_is_accepted);
    RUN_TEST(test_command_helper_send_failure);
    RUN_TEST(test_happy_path_no_auth);
    RUN_TEST(test_auth_login);
    RUN_TEST(test_auth_rejected);
    RUN_TEST(test_greeting_not_ready);
    RUN_TEST(test_rcpt_rejected);
    RUN_TEST(test_data_refused);
    RUN_TEST(test_dot_stuffing);
    RUN_TEST(test_multiline_reply_and_lf_body);
    RUN_TEST(test_partial_reads_dribble);
    RUN_TEST(test_missing_required_arg);
    RUN_TEST(test_io_error_when_server_hangs);
    RUN_TEST(test_reply_buffer_overflow);
    RUN_TEST(test_command_send_fails);
    RUN_TEST(test_body_send_fails);
    RUN_TEST(test_auth_secret_too_long);
    RUN_TEST(test_io_error_at_each_step);
    RUN_TEST(test_protocol_error_at_each_step);
    RUN_TEST(test_command_line_overflows);
    RUN_TEST(test_message_header_overflow);
    RUN_TEST(test_cr_in_body_dropped);
    RUN_TEST(test_build_message_boundary_overflows);
    RUN_TEST(test_host_smtp_send_stub);
    RUN_TEST(test_starttls_upgrades_and_reissues_ehlo);
    RUN_TEST(test_starttls_not_advertised_fails_before_auth);
    RUN_TEST(test_starttls_partial_keyword_is_not_a_match);
    RUN_TEST(test_starttls_capability_match_is_case_insensitive);
    RUN_TEST(test_starttls_server_refuses_the_upgrade);
    RUN_TEST(test_starttls_handshake_failure_aborts);
    RUN_TEST(test_starttls_without_an_upgrade_callback_is_an_arg_error);
    RUN_TEST(test_plain_ignores_an_advertised_starttls);
    return UNITY_END();
}
