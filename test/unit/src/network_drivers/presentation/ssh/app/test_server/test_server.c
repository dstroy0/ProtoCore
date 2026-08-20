// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// app/server.c (RFC 4254 sec 6.5): the file-transfer services the connection protocol starts.
//
// "The program can be a shell, an application program, or a subsystem with a host-independent
// name." SFTP arrives as a "subsystem" request naming it, SCP as an "exec" of the rcp command.
// Neither is a component of SSH; both are programs sec 6.5 starts over a channel already open as
// "session", which is why the channel keeps its sec 5.1 type and gains a service instead.

#include "network_drivers/presentation/ssh/app/server/server.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include <stdint.h>

#include <unity.h>

static uint8_t ssh_app_server_work[16]; // the borrow an entry takes; SshAppServer never reads it

// The file-transfer request classifier, reached through the app-server namespace.
static proto_bool classify_file_transfer_request(uint8_t slot, uint32_t channel, const uint8_t *rtype,
                                                 uint32_t rtype_len, const uint8_t *payload, size_t len, size_t *off,
                                                 proto_bool *accept)
{
    SshAppServerV.slot = slot;
    SshAppServerV.channel = channel;
    SshAppServerV.req.rtype = rtype;
    SshAppServerV.req.rtype_len = rtype_len;
    SshAppServerV.req.payload = payload;
    SshAppServerV.req.len = len;
    SshAppServerV.req.off = off ? *off : 0u;
    SshAppServerV.accept = accept ? *accept : PROTO_FALSE;
    SshAppServer.classify(ssh_app_server_work);
    if (off)
    {
        *off = SshAppServerV.req.off;
    }
    if (accept)
    {
        *accept = SshAppServerV.accept;
    }
    return SshAppServerV.accept;
}

// The connection layer, reached through its namespace.
static int chan_alloc(uint8_t slot)
{
    SshConnectionV.chan.slot = slot;
    SshConnection.chan_alloc(protocore_ssh_connection_span());
    return SshConnectionV.i32;
}

static void channel_set_sftp_open_cb(SshSftpOpenCb cb)
{
    SshConnectionV.sftp_open_cb = cb;
    SshConnection.set_sftp_open_cb(protocore_ssh_connection_span());
}

static void channel_set_scp_open_cb(SshScpOpenCb cb)
{
    SshConnectionV.scp_open_cb = cb;
    SshConnection.set_scp_open_cb(protocore_ssh_connection_span());
}

#if PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP

static int s_sftp_opens;
static int s_scp_opens;
static uint32_t s_opened_channel;
static char s_scp_command[64];

#if PROTOCORE_ENABLE_SSH_SFTP
static void sftp_open(uint8_t i, uint32_t channel)
{
    (void)i;
    s_sftp_opens++;
    s_opened_channel = channel;
}
#endif
#if PROTOCORE_ENABLE_SSH_SCP
static void scp_open(uint8_t i, uint32_t channel, const char *cmd, size_t cmd_len)
{
    (void)i;
    s_scp_opens++;
    s_opened_channel = channel;
    size_t k = 0;
    for (; k < cmd_len && k + 1 < sizeof(s_scp_command); k++)
    {
        s_scp_command[k] = cmd[k];
    }
    s_scp_command[k] = '\0';
}
#endif

// A channel open as "session", which is what both requests arrive on.
static uint32_t open_session_channel(void)
{
    const int cid = chan_alloc(0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, cid);
    SshChannel *c = &ssh_chan[0][cid];
    c->open = PROTO_TRUE;
    c->pending = PROTO_FALSE;
    c->type = SSH_CHAN_SESSION;
    c->service = SSH_CHAN_SERVICE_NONE;
    c->local_id = (uint32_t)cid;
    c->peer_id = 77;
    return (uint32_t)cid;
}

void setUp(void)
{
    SshConnectionV.chan.slot = 0;
    SshConnection.channel_init(protocore_ssh_connection_span());
    s_sftp_opens = 0;
    s_scp_opens = 0;
    s_opened_channel = 0xFFFFFFFFu;
    s_scp_command[0] = '\0';
#if PROTOCORE_ENABLE_SSH_SFTP
    channel_set_sftp_open_cb(sftp_open);
#endif
#if PROTOCORE_ENABLE_SSH_SCP
    channel_set_scp_open_cb(scp_open);
#endif
}
void tearDown(void)
{
#if PROTOCORE_ENABLE_SSH_SFTP
    channel_set_sftp_open_cb(NULL);
#endif
#if PROTOCORE_ENABLE_SSH_SCP
    channel_set_scp_open_cb(NULL);
#endif
}

static size_t put_str(uint8_t *p, size_t off, const char *s, uint32_t n)
{
    p[off] = (uint8_t)(n >> 24);
    p[off + 1] = (uint8_t)(n >> 16);
    p[off + 2] = (uint8_t)(n >> 8);
    p[off + 3] = (uint8_t)n;
    for (uint32_t k = 0; k < n; k++)
    {
        p[off + 4 + k] = (uint8_t)s[k];
    }
    return off + 4 + n;
}

// The classifier is handed the request name and an offset at the request-specific argument, which
// is where the connection layer leaves it after reading name and want_reply.
static proto_bool classify(const char *rtype, uint32_t rtype_len, const char *arg, uint32_t arg_len, uint32_t channel)
{
    uint8_t payload[128];
    const size_t n = put_str(payload, 0, arg, arg_len);
    size_t off = 0;
    proto_bool accept = PROTO_FALSE;
    classify_file_transfer_request(0, channel, (const uint8_t *)rtype, rtype_len, payload, n, &off, &accept);
    return accept;
}

#if PROTOCORE_ENABLE_SSH_SFTP
// ---------------------------------------------------------------------------
// sec 6.5  subsystem "sftp"
// ---------------------------------------------------------------------------

// "a subsystem with a host-independent name" - the name is what selects the program, and it is not
// in the base accept set, so the classifier is what accepts it.
static void test_sec6_5_sftp_subsystem_is_accepted_and_bound(void)
{
    const uint32_t ch = open_session_channel();
    TEST_ASSERT_TRUE(classify("subsystem", 9, "sftp", 4, ch));
    TEST_ASSERT_EQUAL_INT(1, s_sftp_opens);
    TEST_ASSERT_EQUAL_UINT32(ch, s_opened_channel);

    // The channel was opened as "session" (sec 5.1) and stays that; what changed is what its data
    // means from here on.
    TEST_ASSERT_EQUAL(SSH_CHAN_SESSION, ssh_chan[0][ch].type);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_SFTP, ssh_chan[0][ch].service);
}

// A subsystem this end does not run is left alone: not accepted, nothing bound.
static void test_sec6_5_unknown_subsystem_is_not_accepted(void)
{
    const uint32_t ch = open_session_channel();
    TEST_ASSERT_FALSE(classify("subsystem", 9, "netconf", 7, ch));
    TEST_ASSERT_EQUAL_INT(0, s_sftp_opens);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_NONE, ssh_chan[0][ch].service);
}

// The name has to match whole: a prefix is a different subsystem.
static void test_sec6_5_subsystem_name_must_match_whole(void)
{
    const uint32_t ch = open_session_channel();
    TEST_ASSERT_FALSE(classify("subsystem", 9, "sftp-server", 11, ch));
    TEST_ASSERT_EQUAL_INT(0, s_sftp_opens);
}

// A subsystem request whose argument string is missing is not that request.
static void test_sec6_5_subsystem_without_its_argument_is_not_accepted(void)
{
    const uint32_t ch = open_session_channel();
    uint8_t payload[4] = {0, 0, 0, 8}; // claims eight bytes that are not there
    size_t off = 0;
    proto_bool accept = PROTO_FALSE;
    classify_file_transfer_request(0, ch, (const uint8_t *)"subsystem", 9, payload, sizeof(payload), &off, &accept);
    TEST_ASSERT_FALSE(accept);
    TEST_ASSERT_EQUAL_INT(0, s_sftp_opens);
}
#endif // PROTOCORE_ENABLE_SSH_SFTP

#if PROTOCORE_ENABLE_SSH_SCP
// ---------------------------------------------------------------------------
// sec 6.5  exec "scp ..."
// ---------------------------------------------------------------------------

// "This message will request that the server start the execution of the given command." The command
// names scp, so the channel is bound to that service and the command goes with it.
static void test_sec6_5_scp_exec_binds_and_carries_the_command(void)
{
    const uint32_t ch = open_session_channel();
    (void)classify("exec", 4, "scp -t /tmp/out", 15, ch);
    TEST_ASSERT_EQUAL_INT(1, s_scp_opens);
    TEST_ASSERT_EQUAL_UINT32(ch, s_opened_channel);
    TEST_ASSERT_EQUAL_STRING("scp -t /tmp/out", s_scp_command);
    TEST_ASSERT_EQUAL(SSH_CHAN_SESSION, ssh_chan[0][ch].type);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_SCP, ssh_chan[0][ch].service);
}

// Any other command is an ordinary exec, which the connection layer already accepts on its own.
static void test_sec6_5_other_exec_commands_are_not_scp(void)
{
    const uint32_t ch = open_session_channel();
    (void)classify("exec", 4, "whoami", 6, ch);
    TEST_ASSERT_EQUAL_INT(0, s_scp_opens);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_NONE, ssh_chan[0][ch].service);
}

// "scp" without the separating space is a different program name.
static void test_sec6_5_scp_must_be_followed_by_a_space(void)
{
    const uint32_t ch = open_session_channel();
    (void)classify("exec", 4, "scpx -t /tmp", 12, ch);
    TEST_ASSERT_EQUAL_INT(0, s_scp_opens);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_NONE, ssh_chan[0][ch].service);
}

// A command shorter than the name cannot be it.
static void test_sec6_5_short_exec_command_is_not_scp(void)
{
    const uint32_t ch = open_session_channel();
    (void)classify("exec", 4, "sc", 2, ch);
    TEST_ASSERT_EQUAL_INT(0, s_scp_opens);
}
#endif // PROTOCORE_ENABLE_SSH_SCP

// ---------------------------------------------------------------------------
// requests that name no file-transfer service
// ---------------------------------------------------------------------------

// "shell" starts the user's default shell; it names no subsystem and binds nothing here.
static void test_sec6_5_shell_binds_no_service(void)
{
    const uint32_t ch = open_session_channel();
    (void)classify("shell", 5, "", 0, ch);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_NONE, ssh_chan[0][ch].service);
}

// Nor does a request from another section.
static void test_other_request_names_bind_no_service(void)
{
    const uint32_t ch = open_session_channel();
    (void)classify("pty-req", 7, "xterm", 5, ch);
    (void)classify("env", 3, "LANG", 4, ch);
    (void)classify("window-change", 13, "", 0, ch);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_NONE, ssh_chan[0][ch].service);
}

// A channel number with no open channel behind it binds nothing and fires nothing.
static void test_unknown_channel_binds_nothing(void)
{
    const uint32_t ch = open_session_channel();
    ssh_chan[0][ch].open = PROTO_FALSE; // closed under it
#if PROTOCORE_ENABLE_SSH_SFTP
    (void)classify("subsystem", 9, "sftp", 4, ch);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_NONE, ssh_chan[0][ch].service);
#endif
#if PROTOCORE_ENABLE_SSH_SCP
    (void)classify("exec", 4, "scp -t /tmp", 11, ch);
    TEST_ASSERT_EQUAL(SSH_CHAN_SERVICE_NONE, ssh_chan[0][ch].service);
#endif
}

int main(void)
{
    UNITY_BEGIN();
#if PROTOCORE_ENABLE_SSH_SFTP
    RUN_TEST(test_sec6_5_sftp_subsystem_is_accepted_and_bound);
    RUN_TEST(test_sec6_5_unknown_subsystem_is_not_accepted);
    RUN_TEST(test_sec6_5_subsystem_name_must_match_whole);
    RUN_TEST(test_sec6_5_subsystem_without_its_argument_is_not_accepted);
#endif
#if PROTOCORE_ENABLE_SSH_SCP
    RUN_TEST(test_sec6_5_scp_exec_binds_and_carries_the_command);
    RUN_TEST(test_sec6_5_other_exec_commands_are_not_scp);
    RUN_TEST(test_sec6_5_scp_must_be_followed_by_a_space);
    RUN_TEST(test_sec6_5_short_exec_command_is_not_scp);
#endif
    RUN_TEST(test_sec6_5_shell_binds_no_service);
    RUN_TEST(test_other_request_names_bind_no_service);
    RUN_TEST(test_unknown_channel_binds_nothing);
    return UNITY_END();
}

#else // PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP

void setUp(void)
{
}
void tearDown(void)
{
}

static void test_file_transfer_is_not_built(void)
{
    TEST_IGNORE_MESSAGE("neither PROTOCORE_ENABLE_SSH_SFTP nor PROTOCORE_ENABLE_SSH_SCP is on");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_file_transfer_is_not_built);
    return UNITY_END();
}

#endif
