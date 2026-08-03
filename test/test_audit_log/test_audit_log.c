// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the hash-chained audit log (services/security/audit_log). Verify the
// chain detects any tampering (message or hash), that the moving anchor keeps the
// retained window verifiable after the ring wraps, and that the sink + JSON
// rendering behave. SHA-256 is exercised through pc_sha256.

#include "services/security/audit_log/audit_log.h"
#include "shared_primitives/hex.h"
#include <string.h>
#include <unity.h>

void setUp()
{
    pc_audit_reset();
    pc_audit_set_sink(NULL);
}
void tearDown()
{
}

// Tamper helper: the test owns the storage, so cast away const to corrupt it.
static pc_audit_entry *mutable_at(uint16_t i)
{
    return const_cast<pc_audit_entry *>(pc_audit_at(i));
}

void test_append_assigns_monotonic_seq()
{
    TEST_ASSERT_EQUAL_UINT32(1, pc_audit_append(PC_AUDIT_AUTH, "login alice"));
    TEST_ASSERT_EQUAL_UINT32(2, pc_audit_append(PC_AUDIT_AUTH_FAIL, "bad password bob"));
    TEST_ASSERT_EQUAL_UINT32(3, pc_audit_append(PC_AUDIT_CONFIG, "set http_port=80"));
    TEST_ASSERT_EQUAL_UINT16(3, pc_audit_count());
    TEST_ASSERT_EQUAL_STRING("login alice", pc_audit_at(0)->msg);
    TEST_ASSERT_EQUAL_STRING("set http_port=80", pc_audit_at(2)->msg);
    TEST_ASSERT_NULL(pc_audit_at(3));
}

void test_chain_verifies_when_untouched()
{
    for (int i = 0; i < 10; i++)
    {
        pc_audit_append(PC_AUDIT_ACCESS, "GET /resource");
    }
    uint32_t broken = 999;
    TEST_ASSERT_TRUE(pc_audit_verify(&broken));
    TEST_ASSERT_EQUAL_UINT32(999, broken); // untouched on success
}

void test_tampered_message_breaks_chain()
{
    for (int i = 0; i < 6; i++)
    {
        pc_audit_append(PC_AUDIT_SYSTEM, "tick");
    }
    // Corrupt record #4's message in place (hash now mismatches its fields).
    pc_audit_entry *e = mutable_at(3);
    strcpy(e->msg, "EVIL");
    uint32_t broken = 0;
    TEST_ASSERT_FALSE(pc_audit_verify(&broken));
    TEST_ASSERT_EQUAL_UINT32(4, broken); // seq of the first failing record
}

void test_tampered_hash_breaks_chain()
{
    for (int i = 0; i < 5; i++)
    {
        pc_audit_append(PC_AUDIT_SYSTEM, "tick");
    }
    mutable_at(2)->hash[0] ^= 0xFF; // flip a hash bit
    uint32_t broken = 0;
    TEST_ASSERT_FALSE(pc_audit_verify(&broken));
    TEST_ASSERT_EQUAL_UINT32(3, broken);
}

// Re-pointing a record to a different category must also break the chain.
void test_tampered_category_breaks_chain()
{
    for (int i = 0; i < 4; i++)
    {
        pc_audit_append(PC_AUDIT_ACCESS, "ok");
    }
    mutable_at(1)->category = PC_AUDIT_ADMIN;
    TEST_ASSERT_FALSE(pc_audit_verify(NULL));
}

void test_ring_evicts_oldest_and_still_verifies()
{
    const int extra = 8;
    for (int i = 0; i < PC_AUDIT_LOG_ENTRIES + extra; i++)
    {
        pc_audit_append(PC_AUDIT_SYSTEM, "msg");
    }
    // Ring is capped; oldest seq advanced past the evicted records.
    TEST_ASSERT_EQUAL_UINT16(PC_AUDIT_LOG_ENTRIES, pc_audit_count());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(extra + 1), pc_audit_at(0)->seq);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(PC_AUDIT_LOG_ENTRIES + extra),
                             pc_audit_at((uint16_t)(pc_audit_count() - 1))->seq);
    // The moving anchor keeps the retained window a complete, verifiable chain.
    TEST_ASSERT_TRUE(pc_audit_verify(NULL));
}

// After the ring wraps, tampering the OLDEST retained record is still caught -
// the moving anchor makes even the first retained record verifiable.
void test_tamper_after_wrap_detected_at_oldest()
{
    for (int i = 0; i < PC_AUDIT_LOG_ENTRIES + 5; i++)
    {
        pc_audit_append(PC_AUDIT_SYSTEM, "x");
    }
    pc_audit_entry *oldest = mutable_at(0);
    oldest->msg[0] = (oldest->msg[0] == 'x') ? 'y' : 'x';
    uint32_t broken = 0;
    TEST_ASSERT_FALSE(pc_audit_verify(&broken));
    TEST_ASSERT_EQUAL_UINT32(oldest->seq, broken);
}

void test_reset_clears_everything()
{
    pc_audit_append(PC_AUDIT_AUTH, "a");
    pc_audit_append(PC_AUDIT_AUTH, "b");
    pc_audit_reset();
    TEST_ASSERT_EQUAL_UINT16(0, pc_audit_count());
    TEST_ASSERT_TRUE(pc_audit_verify(NULL)); // empty chain is trivially intact
    // Sequence restarts at 1 after reset.
    TEST_ASSERT_EQUAL_UINT32(1, pc_audit_append(PC_AUDIT_SYSTEM, "fresh"));
}

// Sink receives every record at append time (the durable-forwarding path).
static int s_sink_calls = 0;
static uint32_t s_sink_last_seq = 0;
static char s_sink_last_msg[PC_AUDIT_MSG_LEN];
static void test_sink(const pc_audit_entry *e)
{
    s_sink_calls++;
    s_sink_last_seq = e->seq;
    strncpy(s_sink_last_msg, e->msg, sizeof(s_sink_last_msg) - 1);
}

void test_sink_receives_each_record()
{
    s_sink_calls = 0;
    pc_audit_set_sink(test_sink);
    pc_audit_append(PC_AUDIT_AUTH, "one");
    pc_audit_append(PC_AUDIT_AUTH, "two");
    pc_audit_append(PC_AUDIT_AUTH, "three");
    TEST_ASSERT_EQUAL_INT(3, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT32(3, s_sink_last_seq);
    TEST_ASSERT_EQUAL_STRING("three", s_sink_last_msg);
}

void test_format_and_dump_json()
{
    pc_audit_append(PC_AUDIT_AUTH, "login \"alice\"\n"); // forces JSON escaping
    char one[256];
    int n = pc_audit_format(pc_audit_at(0), one, sizeof(one));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(one, "\"seq\":1"));
    TEST_ASSERT_NOT_NULL(strstr(one, "\"cat\":\"auth\""));
    TEST_ASSERT_NOT_NULL(strstr(one, "\\\"alice\\\"")); // quote escaped
    TEST_ASSERT_NOT_NULL(strstr(one, "\\n"));           // newline escaped

    char doc[512];
    int dn = pc_audit_dump_json(doc, sizeof(doc));
    TEST_ASSERT_TRUE(dn > 0);
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"intact\":true"));
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"count\":1"));
}

void test_dump_json_reports_broken_chain()
{
    for (int i = 0; i < 4; i++)
    {
        pc_audit_append(PC_AUDIT_SYSTEM, "ok");
    }
    mutable_at(2)->msg[0] ^= 0xFF;
    char doc[1024]; // 4 records * (~64 hex hash + fields)
    TEST_ASSERT_TRUE(pc_audit_dump_json(doc, sizeof(doc)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"intact\":false"));
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"first_broken\":3"));
}

void test_format_fails_closed_on_small_buffer()
{
    pc_audit_append(PC_AUDIT_AUTH, "some message here");
    char tiny[8];
    TEST_ASSERT_EQUAL_INT(0, pc_audit_format(pc_audit_at(0), tiny, sizeof(tiny)));
}

// A NULL message stores an empty string; each category renders its name.
void test_null_msg_and_categories()
{
    pc_audit_append(PC_AUDIT_SYSTEM, NULL);
    TEST_ASSERT_EQUAL_STRING("", pc_audit_at(0)->msg);

    pc_audit_reset();
    pc_audit_append(PC_AUDIT_AUTH_FAIL, "a");
    pc_audit_append(PC_AUDIT_ACCESS, "b");
    pc_audit_append(PC_AUDIT_CONFIG, "c");
    pc_audit_append(PC_AUDIT_ADMIN, "d");
    char doc[1024];
    TEST_ASSERT_TRUE(pc_audit_dump_json(doc, sizeof(doc)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"cat\":\"auth_fail\""));
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"cat\":\"access\""));
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"cat\":\"config\""));
    TEST_ASSERT_NOT_NULL(strstr(doc, "\"cat\":\"admin\""));
}

// Every JSON escape branch: backslash, tab, CR, and a \u00XX control char.
void test_json_escape_all_chars()
{
    pc_audit_append(PC_AUDIT_SYSTEM, "a\\b\tc\r\x01");
    char buf[256];
    TEST_ASSERT_TRUE(pc_audit_format(pc_audit_at(0), buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\\\"));    // backslash
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\t"));     // tab
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\r"));     // carriage return
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\u0001")); // control char
}

// pc_audit_format fails closed for a null arg and for every buffer size below
// the full length (walking cap across each stage: head, escape, mid, hash, close).
void test_format_fails_closed_all_stages()
{
    pc_audit_append(PC_AUDIT_AUTH, "hi\tthere");
    const pc_audit_entry *e = pc_audit_at(0);
    char full[256];
    int flen = pc_audit_format(e, full, sizeof(full));
    TEST_ASSERT_TRUE(flen > 0);
    TEST_ASSERT_EQUAL_INT(0, pc_audit_format(e, full, 0));
    TEST_ASSERT_EQUAL_INT(0, pc_audit_format(NULL, full, sizeof(full)));
    for (int cap = 1; cap < flen; cap++)
    {
        char small[256];
        TEST_ASSERT_EQUAL_INT(0, pc_audit_format(e, small, (size_t)cap));
    }
}

// pc_audit_dump_json fails closed for a null arg and every under-sized buffer
// (head, the inter-record comma, a truncated entry, and the closing bytes).
void test_dump_fails_closed_all_stages()
{
    for (int i = 0; i < 3; i++)
    {
        pc_audit_append(PC_AUDIT_SYSTEM, "rec");
    }
    char full[1024];
    int flen = pc_audit_dump_json(full, sizeof(full));
    TEST_ASSERT_TRUE(flen > 0);
    TEST_ASSERT_EQUAL_INT(0, pc_audit_dump_json(full, 0));
    TEST_ASSERT_EQUAL_INT(0, pc_audit_dump_json(NULL, sizeof(full)));
    for (int cap = 1; cap < flen; cap++)
    {
        char small[1024];
        TEST_ASSERT_EQUAL_INT(0, pc_audit_dump_json(small, (size_t)cap));
    }
}

// pc_audit_format's guard clause is `!e || !out || cap == 0`; the other tests
// exercise a null entry and a zero cap but never a null output buffer - cover
// that third arm directly.
void test_format_fails_closed_on_null_out(void)
{
    pc_audit_append(PC_AUDIT_AUTH, "x");
    const pc_audit_entry *e = pc_audit_at(0);
    char full[256];
    TEST_ASSERT_TRUE(pc_audit_format(e, full, sizeof(full)) > 0);
    TEST_ASSERT_EQUAL_INT(0, pc_audit_format(e, NULL, sizeof(full)));
}

// pc_audit_dump_json's closing-brace check needs cap strictly greater than the
// full rendered length (one extra byte for the NUL); cap == flen exactly is the
// one value the cap-sweep in test_dump_fails_closed_all_stages never reaches
// (it stops at flen - 1), so probe that exact boundary directly.
void test_dump_fails_closed_at_exact_length(void)
{
    for (int i = 0; i < 3; i++)
    {
        pc_audit_append(PC_AUDIT_SYSTEM, "rec");
    }
    char full[1024];
    int flen = pc_audit_dump_json(full, sizeof(full));
    TEST_ASSERT_TRUE(flen > 0);
    char exact[1024];
    TEST_ASSERT_EQUAL_INT(0, pc_audit_dump_json(exact, (size_t)flen));
}

// pc_hex_digit's uppercase arm isn't reached by any other call in this env's
// build_src_filter (audit_log.cpp only ever asks for lowercase); exercise both
// arms directly.
void test_hex_digit_upper_and_lower(void)
{
    TEST_ASSERT_EQUAL_CHAR('a', pc_hex_digit(10, false));
    TEST_ASSERT_EQUAL_CHAR('A', pc_hex_digit(10, true));
    TEST_ASSERT_EQUAL_CHAR('f', pc_hex_digit(15, false));
    TEST_ASSERT_EQUAL_CHAR('F', pc_hex_digit(15, true));
}

// pc_hex_val: none of audit_log.cpp's call sites use it (it only ever encodes,
// never decodes), so no other test in this env's build reaches it. Cover all
// three matching arms (digit, lower a-f, upper A-F) and the fall-through -1.
void test_hex_val_all_arms(void)
{
    TEST_ASSERT_EQUAL_INT(0, pc_hex_val('0'));
    TEST_ASSERT_EQUAL_INT(9, pc_hex_val('9'));
    TEST_ASSERT_EQUAL_INT(10, pc_hex_val('a'));
    TEST_ASSERT_EQUAL_INT(15, pc_hex_val('f'));
    TEST_ASSERT_EQUAL_INT(10, pc_hex_val('A'));
    TEST_ASSERT_EQUAL_INT(15, pc_hex_val('F'));
    TEST_ASSERT_EQUAL_INT(-1, pc_hex_val('g'));
    TEST_ASSERT_EQUAL_INT(-1, pc_hex_val('!'));
}

// pc_hex_encode is unused by audit_log.cpp (it builds hex text one nibble at a
// time via pc_hex_digit instead); exercise it directly, both case arms and the
// n==0 loop-skip.
void test_hex_encode_upper_and_lower(void)
{
    const uint8_t bytes[3] = {0x0A, 0xFF, 0x01};
    char out[7];
    pc_hex_encode(bytes, 3, out, false); // lowercase
    TEST_ASSERT_EQUAL_STRING("0aff01", out);
    pc_hex_encode(bytes, 3, out, true); // uppercase
    TEST_ASSERT_EQUAL_STRING("0AFF01", out);
    char empty_out[1];
    pc_hex_encode(bytes, 0, empty_out, false); // loop body never runs
    TEST_ASSERT_EQUAL_STRING("", empty_out);
}

// pc_hex_decode is unused by audit_log.cpp (the log is append/verify-only, it
// never decodes hex back to bytes); exercise every branch directly: odd length,
// length within cap but decoded size over cap, an invalid high nibble, an
// invalid low nibble (high still valid), and full round-trip success.
void test_hex_decode_all_branches(void)
{
    uint8_t out[4];
    TEST_ASSERT_EQUAL_INT(-1, pc_hex_decode("abc", 3, out, sizeof(out))); // odd length
    TEST_ASSERT_EQUAL_INT(-1, pc_hex_decode("aabbccdd", 8, out, 2));      // 4 bytes > cap 2
    TEST_ASSERT_EQUAL_INT(-1, pc_hex_decode("g0", 2, out, sizeof(out)));  // bad hi nibble
    TEST_ASSERT_EQUAL_INT(-1, pc_hex_decode("0g", 2, out, sizeof(out)));  // bad lo nibble
    TEST_ASSERT_EQUAL_INT(1, pc_hex_decode("aA", 2, out, sizeof(out)));   // 1 byte decoded
    TEST_ASSERT_EQUAL_UINT8(0xAA, out[0]);
    TEST_ASSERT_EQUAL_INT(3, pc_hex_decode("0aFF01", 6, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(0x0A, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[2]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_append_assigns_monotonic_seq);
    RUN_TEST(test_chain_verifies_when_untouched);
    RUN_TEST(test_tampered_message_breaks_chain);
    RUN_TEST(test_tampered_hash_breaks_chain);
    RUN_TEST(test_tampered_category_breaks_chain);
    RUN_TEST(test_ring_evicts_oldest_and_still_verifies);
    RUN_TEST(test_tamper_after_wrap_detected_at_oldest);
    RUN_TEST(test_reset_clears_everything);
    RUN_TEST(test_sink_receives_each_record);
    RUN_TEST(test_format_and_dump_json);
    RUN_TEST(test_dump_json_reports_broken_chain);
    RUN_TEST(test_format_fails_closed_on_small_buffer);
    RUN_TEST(test_null_msg_and_categories);
    RUN_TEST(test_json_escape_all_chars);
    RUN_TEST(test_format_fails_closed_all_stages);
    RUN_TEST(test_dump_fails_closed_all_stages);
    RUN_TEST(test_format_fails_closed_on_null_out);
    RUN_TEST(test_dump_fails_closed_at_exact_length);
    RUN_TEST(test_hex_digit_upper_and_lower);
    RUN_TEST(test_hex_val_all_arms);
    RUN_TEST(test_hex_encode_upper_and_lower);
    RUN_TEST(test_hex_decode_all_branches);
    return UNITY_END();
}
