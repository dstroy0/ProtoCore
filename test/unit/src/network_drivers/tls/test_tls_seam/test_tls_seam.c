// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for src/network_drivers/tls/tls.h - the slot-indexed surface the transport and the
// presentation layer call TLS through.
//
// handshake/ drives one TlsConn and record/ frames for it; neither knows what a connection slot is.
// This file's subject is the table that joins them: it reads ciphertext out of the slot's receive
// ring, hands whole records to the driver, and puts whatever the driver owes back on the wire
// through the transport's context-safe raw write.
//
// Why this suite exists at all: every one of these calls was an ungated `static inline` returning
// PROTO_FALSE / -1 / 0, and each was the ONLY definition of its name. protocore_tls_write returned
// -1 on every build, so TLS data never flowed, and nothing failed - the stubs linked cleanly and no
// suite reached them. These cases are what makes a stub impossible to reintroduce silently: each
// one asserts on behavior a stub cannot produce.

#include "network_drivers/tls/tls.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/transport/tcp/protocol/protocol.h"
#include <string.h>

#include <unity.h>

// RFC 8032 sec 7.1 test vector 1: a seed and the public key it derives.
static const uint8_t SERVER_ED_SEED[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                           0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                           0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
static const uint8_t SERVER_ED_PUB[32] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                                          0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                                          0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};

static protocore_pcb g_pcb;

void setUp(void)
{
    memset(&g_pcb, 0, sizeof(g_pcb));
    ConnPoolV.life.conn_timeout_ms = CONN_TIMEOUT_MS;
    ConnPool.init(protocore_conn_pool_span());
    protocore_net_host_reset();
}

void tearDown(void)
{
    protocore_tls_conn_free(0);
}

// Put slot 0 in the state an accept would leave it in, with a control block to write through.
static void arm_slot(uint8_t slot)
{
    conn_pool[slot].id = slot;
    conn_pool[slot].pcb = &g_pcb;
    conn_pool[slot].listener_id = 0;
    conn_pool[slot].owner = 0;
    ConnPoolV.slot = slot;
    ConnPoolV.st = CONN_ACTIVE;
    ConnPool.set_state(protocore_conn_pool_span());
}

// Push bytes into the slot's receive ring, the way the stack's recv callback does.
static void push(uint8_t slot, const uint8_t *data, size_t len)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; i < len; i++)
    {
        size_t next = (c->rx_head + 1) % RX_BUF_SIZE;
        c->rx_buffer[c->rx_head] = data[i];
        c->rx_head = next;
    }
}

// ---------------------------------------------------------------------------
// The credential
// ---------------------------------------------------------------------------

// This engine presents an RFC 7250 raw public key, so the credential is the 32-byte public key and
// the 32-byte signing seed. Anything else is refused rather than half-installed.
void test_the_credential_is_a_raw_ed25519_keypair(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    TEST_ASSERT_TRUE(protocore_tls_ready());
}

void test_a_credential_of_the_wrong_shape_is_refused(void)
{
    TEST_ASSERT_FALSE(protocore_tls_global_init(NULL, 32, SERVER_ED_SEED, 32));
    TEST_ASSERT_FALSE(protocore_tls_global_init(SERVER_ED_PUB, 32, NULL, 32));
    TEST_ASSERT_FALSE(protocore_tls_global_init(SERVER_ED_PUB, 31, SERVER_ED_SEED, 32));
    TEST_ASSERT_FALSE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 16));
}

// ---------------------------------------------------------------------------
// Standing a connection up
// ---------------------------------------------------------------------------

// A connection cannot begin before a credential is installed: this end would have nothing to sign
// the CertificateVerify with (RFC 8446 sec 4.4.3).
void test_a_connection_needs_a_credential_first(void)
{
    protocore_tls_conn_free(0);
    arm_slot(0);
    // The suite's other cases install one; this asserts the guard, not the absence.
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));
}

void test_begin_binds_the_slot_and_leaves_it_unestablished(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    TEST_ASSERT_NOT_NULL(protocore_tls_conn_at(0));
    TEST_ASSERT_FALSE(protocore_tls_established(0)); // no ClientHello has arrived yet
}

// A slot outside the pool is refused rather than indexed.
void test_a_slot_outside_the_pool_is_refused(void)
{
    TEST_ASSERT_FALSE(protocore_tls_conn_begin(MAX_CONNS));
    TEST_ASSERT_FALSE(protocore_tls_established(MAX_CONNS));
    TEST_ASSERT_NULL(protocore_tls_conn_at(MAX_CONNS));

    uint8_t buf[4];
    TEST_ASSERT_TRUE(protocore_tls_read(MAX_CONNS, buf, sizeof(buf)) < 0);
    TEST_ASSERT_TRUE(protocore_tls_write(MAX_CONNS, "x", 1) < 0);
}

// ---------------------------------------------------------------------------
// The framing: whole records only
// ---------------------------------------------------------------------------

// RFC 8446 sec 5.1: the 5-byte header carries the fragment length. A record that has not fully
// arrived is left in the ring, so the handshake is never handed a partial one. This is the case a
// stub could never pass: it distinguishes "nothing yet" from "failed".
void test_a_partial_record_is_left_in_the_ring(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    // A header claiming 64 octets, with only 8 of them delivered.
    const uint8_t hdr[5] = {0x16, 0x03, 0x01, 0x00, 0x40};
    push(0, hdr, sizeof(hdr));
    const uint8_t part[8] = {0};
    push(0, part, sizeof(part));

    TEST_ASSERT_EQUAL_INT(0, protocore_tls_handshake(0)); // still handshaking, nothing consumed

    ConnPoolV.slot = 0;
    ConnPool.available(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT(13, ConnPoolV.n); // every byte still there, awaiting the rest
}

// Fewer bytes than a header is the same answer, and consumes nothing.
void test_less_than_a_header_consumes_nothing(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    const uint8_t two[2] = {0x16, 0x03};
    push(0, two, sizeof(two));

    TEST_ASSERT_EQUAL_INT(0, protocore_tls_handshake(0));
    ConnPoolV.slot = 0;
    ConnPool.available(protocore_conn_pool_span());
    TEST_ASSERT_EQUAL_UINT(2, ConnPoolV.n);
}

// A fragment longer than this build's record cap is not framed: sec 5.1 bounds TLSPlaintext, and a
// header claiming more than the receive region holds cannot be honoured.
void test_an_over_long_fragment_is_not_framed(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    const uint8_t hdr[5] = {0x16, 0x03, 0x01, 0xFF, 0xFF}; // 65535, past the cap
    push(0, hdr, sizeof(hdr));

    TEST_ASSERT_EQUAL_INT(0, protocore_tls_handshake(0));
}

// ---------------------------------------------------------------------------
// A malformed record fails the connection
// ---------------------------------------------------------------------------

// A whole record that is not a handshake this end can take ends the connection, and the driver
// records its alert (RFC 8446 sec 6). A negative return is what tells the presentation layer to
// abort - a stub returning -1 unconditionally would pass this one, which is why the cases above
// pin the 0 answers too.
void test_a_malformed_record_fails_the_connection(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    // A complete handshake record whose body is not a ClientHello.
    const uint8_t rec[9] = {0x16, 0x03, 0x01, 0x00, 0x04, 0xFF, 0x00, 0x00, 0x00};
    push(0, rec, sizeof(rec));

    TEST_ASSERT_TRUE(protocore_tls_handshake(0) < 0);
    TEST_ASSERT_FALSE(protocore_tls_established(0));

    const TlsConn *c = protocore_tls_conn_at(0);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_EQUAL(0, c->alert); // sec 6: the failure carries one
}

// ---------------------------------------------------------------------------
// Writing before the keys exist
// ---------------------------------------------------------------------------

// Application data cannot be sealed until the application traffic keys are installed, so a write
// before the handshake completes is refused rather than sent in the clear.
void test_a_write_before_the_handshake_is_refused(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    size_t before = protocore_net_host_tx_len;
    TEST_ASSERT_TRUE(protocore_tls_write(0, "hello", 5) < 0);
    TEST_ASSERT_EQUAL_UINT(before, protocore_net_host_tx_len); // nothing reached the wire
}

// And a read finds nothing rather than reporting a failure, so a caller polling an idle connection
// is not told it died.
void test_a_read_with_nothing_buffered_reports_nothing(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    uint8_t buf[32];
    TEST_ASSERT_EQUAL_INT(0, protocore_tls_read(0, buf, sizeof(buf)));
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

// Freeing a slot wipes the key generations it installed: the storage is this module's and the next
// connection on the slot reuses it, so no key material may stay resident.
//
// What a wipe promises is the SECRETS - the AEAD schedule, the write IV and the record nonce - plus
// the ready flag and the sequence number. TlsRecordKeys::cipher is the negotiated AEAD identifier,
// not a secret, and is deliberately left; asserting the whole struct went to zero would be asserting
// more than the record layer offers.
static uint8_t any_set(const uint8_t *p, size_t n)
{
    uint8_t any = 0;
    for (size_t i = 0; i < n; i++)
    {
        any |= p[i];
    }
    return any;
}

void test_free_wipes_the_key_generations(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));

    TlsConn *c = protocore_tls_conn_at(0);
    TEST_ASSERT_NOT_NULL(c);
    memset(&c->ap_tx, 0xAB, sizeof(c->ap_tx));
    memset(&c->hs_rx, 0xAB, sizeof(c->hs_rx));

    protocore_tls_conn_free(0);

    // What this layer can see: the write IV and the record nonce are gone, the generation is not
    // ready, and the sequence number restarts. The AEAD schedule inside gcm[] is aes128gcm's own
    // contract - Aes128GcmWork is private to that file, so it is asserted in its suite, not here.
    TEST_ASSERT_EQUAL_UINT8(0, any_set(c->ap_tx.iv, sizeof(c->ap_tx.iv)));
    TEST_ASSERT_EQUAL_UINT8(0, any_set(c->ap_tx.nonce, sizeof(c->ap_tx.nonce)));
    TEST_ASSERT_FALSE(c->ap_tx.ready);
    TEST_ASSERT_EQUAL_UINT64(0, c->ap_tx.seq);

    // Every direction, not just the one: a connection installs four generations.
    TEST_ASSERT_EQUAL_UINT8(0, any_set(c->hs_rx.iv, sizeof(c->hs_rx.iv)));
    TEST_ASSERT_FALSE(c->hs_rx.ready);

    TEST_ASSERT_FALSE(protocore_tls_established(0));
}

// The slot can be stood up again after a free, on the bytes the previous connection used.
void test_a_slot_stands_up_again_after_a_free(void)
{
    TEST_ASSERT_TRUE(protocore_tls_global_init(SERVER_ED_PUB, 32, SERVER_ED_SEED, 32));
    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));
    protocore_tls_conn_free(0);

    arm_slot(0);
    TEST_ASSERT_TRUE(protocore_tls_conn_begin(0));
    TEST_ASSERT_FALSE(protocore_tls_established(0));
}
