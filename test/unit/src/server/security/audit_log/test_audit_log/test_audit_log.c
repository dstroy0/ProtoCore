// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the tamper-evident hash-chained audit log (server/security/audit_log/audit_log.h).
//
// No standard governs this record layout, so the expectations below are properties the construction
// must hold whatever the implementation: a chain over SHA-256 (FIPS 180-4) is tamper-evident only if
// EVERY covered field is actually covered. test_chain_hash_is_sha256_over_the_documented_fields is
// the load-bearing case - it rebuilds the first record's hash from `prev || seq_le || ts_le ||
// category || msg_len || msg` with the SHA-256 primitive directly, so the serialization is pinned to
// the documented one rather than to whatever this module happens to feed the hash. The tamper cases
// then walk one field at a time; a field left out of the hash would pass verify() after being
// altered, and each of those cases would fail. The JSON string escaping is RFC 8259 sec 7.

#include "crypto/hash/sha256/sha256.h"
#include "server/clock/clock.h" // Clock.millis(): refresh the stamp a record carries
#include "server/security/audit_log/audit_log.h"
#include <string.h>

#include <unity.h>

// SHA-256 borrows its working set from the caller; the test owns one, aligned for uint32_t.
static uint32_t g_work[(PROTOCORE_SHA256_BORROW + 4) / 4];

void setUp(void)
{
    set_millis(0);
    Clock.millis(Clock.internal); // refresh the stamp audit_log records with
    AuditLog.reset(protocore_audit_log_span());
    AuditLog.set_sink_args.sink = NULL;
    AuditLog.set_sink(protocore_audit_log_span());
}
void tearDown(void)
{
}

// The ring is the test's own storage, so a tamper case writes through the const view.
static protocore_audit_entry *mutable_at(uint16_t i)
{
    AuditLog.at_args.i = i;
    AuditLog.at(protocore_audit_log_span());
    return (protocore_audit_entry *)AuditLog.ptr;
}

static void put_le32(uint8_t out[4], uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
    out[2] = (uint8_t)((v >> 16) & 0xFFu);
    out[3] = (uint8_t)((v >> 24) & 0xFFu);
}

// SHA-256(prev || seq_le || ts_le || category || msg_len || msg), the chain hash's definition.
static void chain_hash(const uint8_t prev[PROTOCORE_AUDIT_HASH_LEN], const protocore_audit_entry *e,
                       uint8_t out[PROTOCORE_AUDIT_HASH_LEN])
{
    uint8_t *c;
    uint8_t le[4];
    c = (uint8_t *)g_work;
    Sha256.init(c);
    Sha256V.update_args.data = prev;
    Sha256V.update_args.len = PROTOCORE_AUDIT_HASH_LEN;
    Sha256.update(c);
    put_le32(le, e->seq);
    Sha256V.update_args.data = le;
    Sha256V.update_args.len = 4;
    Sha256.update(c);
    put_le32(le, e->ts);
    Sha256V.update_args.data = le;
    Sha256V.update_args.len = 4;
    Sha256.update(c);
    const uint8_t cat = (uint8_t)e->category;
    Sha256V.update_args.data = &cat;
    Sha256V.update_args.len = 1;
    Sha256.update(c);
    const uint8_t mlen = (uint8_t)strlen(e->msg);
    Sha256V.update_args.data = &mlen;
    Sha256V.update_args.len = 1;
    Sha256.update(c);
    Sha256V.update_args.data = (const uint8_t *)e->msg;
    Sha256V.update_args.len = mlen;
    Sha256.update(c);
    Sha256V.final_args.out = out;
    Sha256.final(c);
}

// The chain starts from a genesis anchor of 32 zero octets, and the first record's stored hash is
// the SHA-256 of that anchor followed by the record's own fields in the documented order.
void test_chain_hash_is_sha256_over_the_documented_fields(void)
{
    static const uint8_t GENESIS[PROTOCORE_AUDIT_HASH_LEN] = {0};

    set_millis(0x11223344u);
    Clock.millis(Clock.internal); // refresh the stamp audit_log records with
    AuditLog.append_args.category = PROTOCORE_AUDIT_AUTH;
    AuditLog.append_args.msg = "login alice";
    AuditLog.append(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32(1u, AuditLog.ms);

    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    const protocore_audit_entry *e = AuditLog.ptr;
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT32(1u, e->seq);
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, e->ts);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_AUDIT_AUTH, e->category);

    uint8_t want[PROTOCORE_AUDIT_HASH_LEN];
    chain_hash(GENESIS, e, want);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, e->hash, PROTOCORE_AUDIT_HASH_LEN);

    // The second record chains the first's hash, not the genesis anchor.
    set_millis(0x55667788u);
    Clock.millis(Clock.internal); // refresh the stamp audit_log records with
    AuditLog.append_args.category = PROTOCORE_AUDIT_CONFIG;
    AuditLog.append_args.msg = "set http_port=80";
    AuditLog.append(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32(2u, AuditLog.ms);
    AuditLog.at_args.i = 1;
    AuditLog.at(protocore_audit_log_span());
    const protocore_audit_entry *f = AuditLog.ptr;
    chain_hash(e->hash, f, want);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f->hash, PROTOCORE_AUDIT_HASH_LEN);
}

// Sequence numbers are 1-based, monotonic, and each record keeps the message it was appended with.
void test_seq_is_monotonic_from_one(void)
{
    AuditLog.append_args.category = PROTOCORE_AUDIT_AUTH;
    AuditLog.append_args.msg = "login alice";
    AuditLog.append(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32(1u, AuditLog.ms);
    AuditLog.append_args.category = PROTOCORE_AUDIT_AUTH_FAIL;
    AuditLog.append_args.msg = "bad password bob";
    AuditLog.append(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32(2u, AuditLog.ms);
    AuditLog.append_args.category = PROTOCORE_AUDIT_CONFIG;
    AuditLog.append_args.msg = "set http_port=80";
    AuditLog.append(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32(3u, AuditLog.ms);
    AuditLog.count(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT16(3u, AuditLog.value);
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("login alice", AuditLog.ptr->msg);
    AuditLog.at_args.i = 2;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("set http_port=80", AuditLog.ptr->msg);
    AuditLog.at_args.i = 3;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_NULL(AuditLog.ptr);
    AuditLog.at_args.i = 0xFFFFu;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_NULL(AuditLog.ptr);
}

// An untouched chain verifies, and the out-parameter is left alone when it does.
void test_untouched_chain_verifies(void)
{
    for (int i = 0; i < 10; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_ACCESS;
        AuditLog.append_args.msg = "GET /resource";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }
    uint32_t broken = 999u;
    AuditLog.verify_args.first_broken_seq = &broken;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok);
    TEST_ASSERT_EQUAL_UINT32(999u, broken);
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok); // the out-parameter is optional
}

// An empty log is a complete chain of nothing, so it verifies.
void test_empty_log_verifies(void)
{
    AuditLog.count(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT16(0u, AuditLog.value);
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok);
}

// Every field the hash covers, altered one at a time: each must break the chain and each must be
// reported at the seq of the record that was altered.
void test_every_covered_field_is_tamper_evident(void)
{
    for (int i = 0; i < 6; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
        AuditLog.append_args.msg = "tick";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }

    // the message
    strcpy(mutable_at(3)->msg, "EVIL");
    uint32_t broken = 0;
    AuditLog.verify_args.first_broken_seq = &broken;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_FALSE(AuditLog.ok);
    TEST_ASSERT_EQUAL_UINT32(4u, broken);
    strcpy(mutable_at(3)->msg, "tick");
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok); // restoring it restores the chain

    // the category
    mutable_at(1)->category = PROTOCORE_AUDIT_ADMIN;
    broken = 0;
    AuditLog.verify_args.first_broken_seq = &broken;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_FALSE(AuditLog.ok);
    TEST_ASSERT_EQUAL_UINT32(2u, broken);
    mutable_at(1)->category = PROTOCORE_AUDIT_SYSTEM;

    // the timestamp
    mutable_at(2)->ts ^= 0x1000u;
    broken = 0;
    AuditLog.verify_args.first_broken_seq = &broken;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_FALSE(AuditLog.ok);
    TEST_ASSERT_EQUAL_UINT32(3u, broken);
    mutable_at(2)->ts ^= 0x1000u;

    // the sequence number
    mutable_at(4)->seq += 1000u;
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_FALSE(AuditLog.ok);
    mutable_at(4)->seq -= 1000u;

    // the stored hash itself
    mutable_at(0)->hash[0] ^= 0xFFu;
    broken = 0;
    AuditLog.verify_args.first_broken_seq = &broken;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_FALSE(AuditLog.ok);
    TEST_ASSERT_EQUAL_UINT32(1u, broken);
    mutable_at(0)->hash[0] ^= 0xFFu;

    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok);
}

// Reordering two records breaks the chain even though every record is individually genuine: the
// hash of each covers its predecessor's, so the order is part of what is signed.
void test_reordering_breaks_the_chain(void)
{
    for (int i = 0; i < 5; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_ACCESS;
        AuditLog.append_args.msg = "a";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
        AuditLog.append_args.category = PROTOCORE_AUDIT_ADMIN;
        AuditLog.append_args.msg = "b";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }
    AuditLog.at_args.i = 2;
    AuditLog.at(protocore_audit_log_span());
    protocore_audit_entry tmp = *AuditLog.ptr;
    AuditLog.at_args.i = 5;
    AuditLog.at(protocore_audit_log_span());
    *mutable_at(2) = *AuditLog.ptr;
    *mutable_at(5) = tmp;
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_FALSE(AuditLog.ok);
}

// A record whose message repeats an earlier one still hashes differently, because the seq is
// covered - otherwise two identical events would be interchangeable.
void test_identical_messages_hash_differently(void)
{
    AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
    AuditLog.append_args.msg = "same";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;
    AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
    AuditLog.append_args.msg = "same";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;
    // One result member, so the first record is copied out before the second call overwrites it.
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    protocore_audit_entry first = *AuditLog.ptr;
    AuditLog.at_args.i = 1;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_NOT_EQUAL(0, memcmp(first.hash, AuditLog.ptr->hash, PROTOCORE_AUDIT_HASH_LEN));
}

// The ring is fixed: past its depth the oldest record is evicted, the retained window is capped, and
// the surviving sequence numbers are the newest ones. The moving anchor keeps that window a
// complete chain, so it still verifies end to end.
void test_the_retained_window_verifies_after_the_ring_wraps(void)
{
    const int extra = 8;
    for (int i = 0; i < PROTOCORE_AUDIT_LOG_ENTRIES + extra; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
        AuditLog.append_args.msg = "msg";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }
    AuditLog.count(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT16((uint16_t)PROTOCORE_AUDIT_LOG_ENTRIES, AuditLog.value);
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(extra + 1), AuditLog.ptr->seq);
    AuditLog.count(protocore_audit_log_span());
    AuditLog.at_args.i = (uint16_t)(AuditLog.value - 1);
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(PROTOCORE_AUDIT_LOG_ENTRIES + extra), AuditLog.ptr->seq);
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok);
}

// The anchor is what makes eviction safe: after a wrap the OLDEST retained record is still checked
// against it, so altering the record an attacker would expect to be unanchored is still caught.
void test_the_oldest_retained_record_is_still_anchored(void)
{
    for (int i = 0; i < PROTOCORE_AUDIT_LOG_ENTRIES + 5; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
        AuditLog.append_args.msg = "x";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }
    protocore_audit_entry *oldest = mutable_at(0);
    const uint32_t seq = oldest->seq;
    oldest->msg[0] = 'y';
    uint32_t broken = 0;
    AuditLog.verify_args.first_broken_seq = &broken;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_FALSE(AuditLog.ok);
    TEST_ASSERT_EQUAL_UINT32(seq, broken);
}

// A message longer than the record truncates rather than overrunning, and stays null-terminated.
void test_a_long_message_is_truncated(void)
{
    char big[PROTOCORE_AUDIT_MSG_LEN * 2];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
    AuditLog.append_args.msg = big;
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    const protocore_audit_entry *e = AuditLog.ptr;
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_AUDIT_MSG_LEN - 1u, (unsigned)strlen(e->msg));
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok);

    AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
    AuditLog.append_args.msg = NULL;
    AuditLog.append(protocore_audit_log_span());
    // a null message is an empty one, not a dereference
    (void)AuditLog.ms;
    AuditLog.at_args.i = 1;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("", AuditLog.ptr->msg);
    AuditLog.verify_args.first_broken_seq = NULL;
    AuditLog.verify(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.ok);
}

// Reset empties the ring, restarts the sequence at 1, and returns the anchor to genesis - so the
// record appended after it hashes exactly as the very first one did.
void test_reset_returns_the_chain_to_genesis(void)
{
    set_millis(7u);
    Clock.millis(Clock.internal); // refresh the stamp audit_log records with
    AuditLog.append_args.category = PROTOCORE_AUDIT_AUTH;
    AuditLog.append_args.msg = "first";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;
    uint8_t before[PROTOCORE_AUDIT_HASH_LEN];
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    memcpy(before, AuditLog.ptr->hash, PROTOCORE_AUDIT_HASH_LEN);

    for (int i = 0; i < 4; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
        AuditLog.append_args.msg = "noise";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }
    AuditLog.reset(protocore_audit_log_span());
    AuditLog.count(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT16(0u, AuditLog.value);

    set_millis(7u);
    Clock.millis(Clock.internal); // refresh the stamp audit_log records with
    AuditLog.append_args.category = PROTOCORE_AUDIT_AUTH;
    AuditLog.append_args.msg = "first";
    AuditLog.append(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_UINT32(1u, AuditLog.ms);
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, AuditLog.ptr->hash, PROTOCORE_AUDIT_HASH_LEN);
}

// The sink sees each record once, at append time, with the hash already set - which is what lets a
// durable store hold the same chain the RAM window does.
static uint16_t g_sink_calls;
static protocore_audit_entry g_sink_last;

static void capture(const protocore_audit_entry *e)
{
    g_sink_calls++;
    g_sink_last = *e;
}

void test_the_sink_receives_the_complete_record(void)
{
    g_sink_calls = 0;
    memset(&g_sink_last, 0, sizeof(g_sink_last));
    AuditLog.set_sink_args.sink = capture;
    AuditLog.set_sink(protocore_audit_log_span());

    AuditLog.append_args.category = PROTOCORE_AUDIT_ADMIN;
    AuditLog.append_args.msg = "reboot";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;
    TEST_ASSERT_EQUAL_UINT16(1u, g_sink_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, g_sink_last.seq);
    TEST_ASSERT_EQUAL_STRING("reboot", g_sink_last.msg);
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AuditLog.ptr->hash, g_sink_last.hash, PROTOCORE_AUDIT_HASH_LEN);

    AuditLog.set_sink_args.sink = NULL;
    AuditLog.set_sink(protocore_audit_log_span());
    AuditLog.append_args.category = PROTOCORE_AUDIT_ADMIN;
    AuditLog.append_args.msg = "again";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;
    TEST_ASSERT_EQUAL_UINT16(1u, g_sink_calls); // detached
}

// The wire names of the standard categories, and the documented fallback for anything else.
void test_category_names(void)
{
    AuditLog.cat_name_args.category = PROTOCORE_AUDIT_SYSTEM;
    AuditLog.cat_name(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("system", AuditLog.text);
    AuditLog.cat_name_args.category = PROTOCORE_AUDIT_AUTH;
    AuditLog.cat_name(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("auth", AuditLog.text);
    AuditLog.cat_name_args.category = PROTOCORE_AUDIT_AUTH_FAIL;
    AuditLog.cat_name(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("auth_fail", AuditLog.text);
    AuditLog.cat_name_args.category = PROTOCORE_AUDIT_ACCESS;
    AuditLog.cat_name(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("access", AuditLog.text);
    AuditLog.cat_name_args.category = PROTOCORE_AUDIT_CONFIG;
    AuditLog.cat_name(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("config", AuditLog.text);
    AuditLog.cat_name_args.category = PROTOCORE_AUDIT_ADMIN;
    AuditLog.cat_name(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("admin", AuditLog.text);
    AuditLog.cat_name_args.category = (protocore_audit_cat)200;
    AuditLog.cat_name(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_STRING("system", AuditLog.text);
}

// One record renders as a JSON object carrying seq, ts, the category name, the message, and the
// hash as 64 lowercase hex characters.
void test_format_renders_one_record(void)
{
    set_millis(4242u);
    Clock.millis(Clock.internal); // refresh the stamp audit_log records with
    AuditLog.append_args.category = PROTOCORE_AUDIT_AUTH_FAIL;
    AuditLog.append_args.msg = "bad password";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;

    static const char HEAD[] = "{\"seq\":1,\"ts\":4242,\"cat\":\"auth_fail\",\"msg\":\"bad password\",\"hash\":\"";

    char out[512];
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    AuditLog.format_args.entry = AuditLog.ptr;
    AuditLog.format_args.out = out;
    AuditLog.format_args.cap = sizeof(out);
    AuditLog.format(protocore_audit_log_span());
    int n = AuditLog.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT(n, (int)strlen(out));
    TEST_ASSERT_EQUAL_STRING_LEN(HEAD, out, sizeof(HEAD) - 1);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
    TEST_ASSERT_EQUAL_CHAR('"', out[n - 2]);

    // the hash is 64 hex characters, lowercase
    const char *h = strstr(out, "\"hash\":\"");
    TEST_ASSERT_NOT_NULL(h);
    h += 8;
    for (int i = 0; i < PROTOCORE_AUDIT_HASH_LEN * 2; i++)
    {
        char c = h[i];
        TEST_ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    TEST_ASSERT_EQUAL_CHAR('"', h[PROTOCORE_AUDIT_HASH_LEN * 2]);
}

// RFC 8259 sec 7: a quotation mark, a reverse solidus and the control characters must be escaped
// inside a JSON string, or the message closes the string and the document becomes attacker-shaped.
void test_format_escapes_the_message(void)
{
    AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
    AuditLog.append_args.msg = "a\"b\\c\nd\re\tf\x01g";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;

    char out[512];
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    AuditLog.format_args.entry = AuditLog.ptr;
    AuditLog.format_args.out = out;
    AuditLog.format_args.cap = sizeof(out);
    AuditLog.format(protocore_audit_log_span());
    TEST_ASSERT_TRUE(AuditLog.n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\\\"")); // "
    TEST_ASSERT_NOT_NULL(strstr(out, "\\\\")); // backslash
    TEST_ASSERT_NOT_NULL(strstr(out, "\\n"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\\r"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\\t"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\\u0001")); // sec 7: below U+0020 goes to \u00XX
    // exactly two unescaped quotes surround the message, so the string is still one string
    TEST_ASSERT_NOT_NULL(strstr(out, "\"msg\":\"a\\\"b"));
}

// Every capacity short of what the record needs yields 0 and never a partial object: a truncated
// JSON line in a durable store is a record nothing can parse.
void test_format_fails_closed_at_every_short_capacity(void)
{
    AuditLog.append_args.category = PROTOCORE_AUDIT_ACCESS;
    AuditLog.append_args.msg = "GET /x";
    AuditLog.append(protocore_audit_log_span());
    (void)AuditLog.ms;
    char full[512];
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    AuditLog.format_args.entry = AuditLog.ptr;
    AuditLog.format_args.out = full;
    AuditLog.format_args.cap = sizeof(full);
    AuditLog.format(protocore_audit_log_span());
    int need = AuditLog.n;
    TEST_ASSERT_TRUE(need > 0);

    char small[512];
    for (int cap = 0; cap <= need; cap++)
    {
        memset(small, '#', sizeof(small));
        AuditLog.at_args.i = 0;
        AuditLog.at(protocore_audit_log_span());
        AuditLog.format_args.entry = AuditLog.ptr;
        AuditLog.format_args.out = small;
        AuditLog.format_args.cap = (size_t)cap;
        AuditLog.format(protocore_audit_log_span());
        TEST_ASSERT_EQUAL_INT(0, AuditLog.n);
    }
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    AuditLog.format_args.entry = AuditLog.ptr;
    AuditLog.format_args.out = small;
    AuditLog.format_args.cap = (size_t)need + 1u;
    AuditLog.format(protocore_audit_log_span());
    // one octet more than the text is exactly enough, for the NUL
    TEST_ASSERT_EQUAL_INT(need, AuditLog.n);

    AuditLog.format_args.entry = NULL;
    AuditLog.format_args.out = full;
    AuditLog.format_args.cap = sizeof(full);
    AuditLog.format(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_INT(0, AuditLog.n);
    AuditLog.at_args.i = 0;
    AuditLog.at(protocore_audit_log_span());
    AuditLog.format_args.entry = AuditLog.ptr;
    AuditLog.format_args.out = NULL;
    AuditLog.format_args.cap = sizeof(full);
    AuditLog.format(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_INT(0, AuditLog.n);
}

// The dump reports the window and its integrity, and says which record broke when it is broken.
void test_dump_reports_integrity(void)
{
    for (int i = 0; i < 3; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_SYSTEM;
        AuditLog.append_args.msg = "e";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }

    static const char HEAD[] = "{\"intact\":true,\"count\":3,\"entries\":[";

    char out[2048];
    AuditLog.dump_json_args.out = out;
    AuditLog.dump_json_args.cap = sizeof(out);
    AuditLog.dump_json(protocore_audit_log_span());
    int n = AuditLog.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT(n, (int)strlen(out));
    TEST_ASSERT_EQUAL_STRING_LEN(HEAD, out, sizeof(HEAD) - 1);
    TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
    TEST_ASSERT_EQUAL_CHAR(']', out[n - 2]);
    TEST_ASSERT_NULL(strstr(out, "first_broken"));

    mutable_at(1)->msg[0] = 'Z';
    AuditLog.dump_json_args.out = out;
    AuditLog.dump_json_args.cap = sizeof(out);
    AuditLog.dump_json(protocore_audit_log_span());
    n = AuditLog.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"intact\":false"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"first_broken\":2"));
}

// The dump fails closed at every capacity short of what it needs, rather than emitting a document
// that is missing its closing bracket.
void test_dump_fails_closed_at_every_short_capacity(void)
{
    for (int i = 0; i < 3; i++)
    {
        AuditLog.append_args.category = PROTOCORE_AUDIT_ADMIN;
        AuditLog.append_args.msg = "e";
        AuditLog.append(protocore_audit_log_span());
        (void)AuditLog.ms;
    }
    char full[2048];
    AuditLog.dump_json_args.out = full;
    AuditLog.dump_json_args.cap = sizeof(full);
    AuditLog.dump_json(protocore_audit_log_span());
    int need = AuditLog.n;
    TEST_ASSERT_TRUE(need > 0);

    char small[2048];
    for (int cap = 0; cap <= need; cap++)
    {
        AuditLog.dump_json_args.out = small;
        AuditLog.dump_json_args.cap = (size_t)cap;
        AuditLog.dump_json(protocore_audit_log_span());
        TEST_ASSERT_EQUAL_INT(0, AuditLog.n);
    }
    AuditLog.dump_json_args.out = small;
    AuditLog.dump_json_args.cap = (size_t)need + 1u;
    AuditLog.dump_json(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_INT(need, AuditLog.n);
    AuditLog.dump_json_args.out = NULL;
    AuditLog.dump_json_args.cap = sizeof(full);
    AuditLog.dump_json(protocore_audit_log_span());
    TEST_ASSERT_EQUAL_INT(0, AuditLog.n);
}

// An empty log still dumps a well-formed document with an empty entry array.
void test_dump_of_an_empty_log(void)
{
    char out[256];
    AuditLog.dump_json_args.out = out;
    AuditLog.dump_json_args.cap = sizeof(out);
    AuditLog.dump_json(protocore_audit_log_span());
    int n = AuditLog.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"intact\":true,\"count\":0,\"entries\":[]}", out);
}
