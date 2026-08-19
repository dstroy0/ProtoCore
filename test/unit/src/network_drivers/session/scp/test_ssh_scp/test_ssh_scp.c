// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The SCP server's setup and its refusals (network_drivers/session/scp/ssh_scp.c). test_scp covers
// the rcp wire codec; this covers the server that drives it: what begin() binds, what it registers,
// and what the two channel callbacks do with input they cannot act on.
//
// The rcp protocol has no specification. The oracle for the SINK control flow is OpenSSH's observed
// behaviour, stated in the module's own header.
//
// Every record the server sends goes out through SshConnection.channel_send_data, which needs an
// SSH slot bound to a live stream and a TCP connection under it. Nothing in the matrix stands that
// up on the host today, so the transfer itself - control line, streamed data, end-of-record ack -
// is not driven here. What is driven is everything that happens before the first send, and every
// path that returns without one.

#include "network_drivers/session/scp/ssh_scp/ssh_scp.c"

#include <string.h>

#include <unity.h>

static uint8_t suite_mnt_work[16]; // the borrow an entry takes; Mnt never reads it

void setUp(void)
{
    Mnt.ram(suite_mnt_work);
    Mnt.args.backend = Mnt.backend;
    Mnt.mount(suite_mnt_work);
    Mnt.ram_format(suite_mnt_work);
}
void tearDown(void)
{
}

// ---------------------------------------------------------------------------
// The borrow
// ---------------------------------------------------------------------------

// The span is the PROTOCORE_SSH_SCP_BORROW bytes the context lives in, and the control line's
// filename buffer is inside it.
void test_the_span_carves_the_context(void)
{
    uint8_t *work = protocore_ssh_scp_span();
    TEST_ASSERT_NOT_NULL(work);
    TEST_ASSERT_EQUAL_PTR(work, (uint8_t *)SSH_SCP_CTX(work));
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_FILESYSTEM_PATH_MAX, sizeof(SSH_SCP_CTX(work)->leaf));
    TEST_ASSERT_TRUE(sizeof(SshScpCtx) <= PROTOCORE_SSH_SCP_BORROW);

    // A borrow arrives zeroed and root 0 is a valid root, so an unbound server reading 0 would
    // resolve against whatever storage was bound first. The carve seats -1, and this runs before
    // any begin() binds one.
    TEST_ASSERT_EQUAL_INT(-1, SSH_SCP_CTX(work)->root);
}

// Taken once: a second call answers with the same bytes.
void test_the_span_is_taken_once(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_ssh_scp_span(), protocore_ssh_scp_span());
}

// NULL is what a short pool hands over, and begin writes through the context, so it does nothing.
void test_begin_refuses_a_null_borrow(void)
{
    SshConnection.scp_open_cb = NULL;
    SshConnection.set_scp_open_cb(protocore_ssh_connection_span());
    SshScp.begin(NULL);
    TEST_ASSERT_NULL(SshConnection.scp_open_cb);
}

// ---------------------------------------------------------------------------
// What begin binds
// ---------------------------------------------------------------------------

// The root is bound by name, so two servers naming different mounts end up over different storage.
// The handle it hands back is what every later resolve is made against.
void test_begin_binds_its_own_mount(void)
{
    uint8_t *work = protocore_ssh_scp_span();
    SshScp.begin(work);
    TEST_ASSERT_TRUE(SSH_SCP_CTX(work)->root >= 0);

    Fs.mount = "mnt/scp";
    Fs.begin(protocore_filesystem_span());
    TEST_ASSERT_EQUAL_INT(Fs.i32, SSH_SCP_CTX(work)->root);
}

// A handle table starts at -1, not 0: closing handle 0 would take a file another server had open.
void test_begin_marks_every_connection_as_holding_no_file(void)
{
    SshScp.begin(protocore_ssh_scp_span());
    for (int i = 0; i < MAX_SSH_CONNS; i++)
    {
        TEST_ASSERT_EQUAL_INT(-1, scp_conns[i].fh);
    }
}

// The exec and data callbacks are what the channel layer dispatches through, and they are installed
// once: a second begin rebinds the root without registering again.
void test_begin_registers_the_channel_callbacks_once(void)
{
    uint8_t *work = protocore_ssh_scp_span();
    SshScp.begin(work);
    TEST_ASSERT_NOT_NULL(SshConnection.scp_open_cb);
    TEST_ASSERT_NOT_NULL(SshConnection.scp_data_cb);
    TEST_ASSERT_TRUE(SSH_SCP_CTX(work)->registered);

    SshConnection.scp_open_cb = NULL;
    SshConnection.set_scp_open_cb(protocore_ssh_connection_span());
    SshScp.begin(work);
    TEST_ASSERT_NULL(SshConnection.scp_open_cb);
}

// ---------------------------------------------------------------------------
// What the callbacks refuse
// ---------------------------------------------------------------------------

// The connection table is fixed and indexed by the slot the channel layer names, so a slot past its
// end is not a connection this server has.
void test_a_slot_past_the_table_is_not_touched(void)
{
    SshScp.begin(protocore_ssh_scp_span());
    scp_conns[0].active = PROTO_FALSE;
    protocore_scp_on_open((uint8_t)MAX_SSH_CONNS, 1, "scp -t /", 8);
    protocore_scp_on_data((uint8_t)MAX_SSH_CONNS, 1, (const uint8_t *)"C0644 1 a\n", 10);
    TEST_ASSERT_FALSE(scp_conns[0].active);
}

// Data for a channel that never opened a transfer, or for a different channel than the one that
// did, is not part of any record.
void test_data_outside_an_open_transfer_is_dropped(void)
{
    uint8_t *work = protocore_ssh_scp_span();
    SshScp.begin(work);

    scp_conns[0].active = PROTO_FALSE;
    scp_conns[0].cl_len = 0;
    protocore_scp_on_data(0, 7, (const uint8_t *)"C0644 1 a\n", 10);
    TEST_ASSERT_EQUAL_UINT(0, scp_conns[0].cl_len);

    scp_conns[0].active = PROTO_TRUE;
    scp_conns[0].channel = 7;
    scp_conns[0].st = WAIT_CLINE;
    protocore_scp_on_data(0, 9, (const uint8_t *)"C0644 1 a\n", 10);
    TEST_ASSERT_EQUAL_UINT(0, scp_conns[0].cl_len);
}

// A control line arrives a byte at a time over a stream, so the server holds what it has and asks
// for nothing until the newline that ends it.
void test_a_partial_control_line_is_held(void)
{
    uint8_t *work = protocore_ssh_scp_span();
    SshScp.begin(work);

    scp_conns[0].active = PROTO_TRUE;
    scp_conns[0].channel = 4;
    scp_conns[0].slot = 0;
    scp_conns[0].st = WAIT_CLINE;
    scp_conns[0].cl_len = 0;
    scp_conns[0].fh = -1;

    protocore_scp_on_data(0, 4, (const uint8_t *)"C0644 ", 6);
    TEST_ASSERT_EQUAL_UINT(6, scp_conns[0].cl_len);
    protocore_scp_on_data(0, 4, (const uint8_t *)"11 x", 4);
    TEST_ASSERT_EQUAL_UINT(10, scp_conns[0].cl_len);
    TEST_ASSERT_EQUAL_INT(-1, scp_conns[0].fh);
    TEST_ASSERT_TRUE(scp_conns[0].st == WAIT_CLINE);
}
